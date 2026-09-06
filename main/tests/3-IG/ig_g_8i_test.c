/*
 * ig_g_8i_test.c
 *
 *  Created on: Sep 6, 2026
 *      Author: Pedro Marques
 * 
 * IG-G.8i — Three-node physical route-failure validation harness.
 *
 * Test topology:
 *
 *     Node 1 (Gateway) -> Node 2 (Relay) -> Node 3 (Sensor)
 *
 * The traffic-generation harness is active only on Node 1. A test-only
 * diagnostic observer is also started on Node 2 so the physical run can
 * report the actual IG-D/E5D counters owned by the relay. Node 3 runs the
 * normal production application unchanged.
 *
 * The physical test environment allows all three nodes to see each other
 * directly. Therefore production discovery naturally creates:
 *
 *     1/3 -> 1/3
 *
 * rather than the required test topology:
 *
 *     1/3 -> 1/2
 *
 * This test deliberately installs the required temporary route in the
 * existing production route table. No production routing/discovery code
 * is modified.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "core/enp_context.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_failure_coalescer.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "core/routing/enp_routing_runtime.h"

#define ENP_GATEWAY_NODE_ID ((enp_node_id_t)1U)
#define ENP_RELAY_NODE_ID  ((enp_node_id_t)2U)
#define ENP_SENSOR_NODE_ID ((enp_node_id_t)3U)

#define IG_G_8I_NETWORK_ID ((enp_network_id_t)1U)

#define IG_G_8I_ROUTE_POLL_MS       250U
#define IG_G_8I_PACKET_INTERVAL_MS 1000U
#define IG_G_8I_OPERATOR_WINDOW_MS 15000U

#define IG_G_8I_POSITIVE_PACKETS 3U
#define IG_G_8I_FAILURE_PACKETS  3U

#define IG_G_8I_ROUTE_SEQUENCE    1U
#define IG_G_8I_ROUTE_LIFETIME_MS 60000U
#define IG_G_8I_ROUTE_METRIC      1U

#define IG_G_8I_TASK_STACK_WORDS 4096U
#define IG_G_8I_TASK_PRIORITY    5U
#define IG_G_8I_DIAG_WINDOW_MS 120000U
#define IG_G_8I_DIAG_POLL_MS      250U

static const char *TAG = "IG-G.8i";

static StaticTask_t s_task_control;
static StackType_t s_task_stack[IG_G_8I_TASK_STACK_WORDS];
static StaticTask_t s_diag_task_control;
#define IG_G_8I_DIAG_TASK_STACK_WORDS 1536U
static StackType_t s_diag_task_stack[IG_G_8I_DIAG_TASK_STACK_WORDS];

typedef struct {
    enp_context_t *context;
    enp_routing_data_path_t *routing_path;
    unsigned packet_number;
} ig_g_8i_runtime_t;

static ig_g_8i_runtime_t s_runtime;

static bool ig_g_8i_is_gateway(const enp_context_t *context)
{
    return (context != NULL) &&
           (context->network.local.id == ENP_GATEWAY_NODE_ID);
}

static bool ig_g_8i_neighbour_is_active(
    const enp_context_t *context,
    enp_node_id_t node_id)
{
    if (context == NULL) {
        return false;
    }

    const enp_address_t address = {
        .network = IG_G_8I_NETWORK_ID,
        .node = node_id,
    };

    const enp_neighbor_t *const neighbor =
        enp_neighbor_find_const(&context->neighbors, &address);

    return (neighbor != NULL) &&
           (neighbor->state == ENP_NEIGHBOR_STATE_ACTIVE);
}

static bool ig_g_8i_install_test_route(void)
{
    enp_route_table_t *const routes =
        enp_routing_runtime_route_table();

    if (routes == NULL) {
        ESP_LOGE(
            TAG,
            "Cannot install test route: runtime route table unavailable");
        return false;
    }

    enp_route_entry_t entry = {0};

    entry.destination = (enp_route_destination_t) {
        .network_id = IG_G_8I_NETWORK_ID,
        .node_id = ENP_SENSOR_NODE_ID,
    };

    entry.next_hop = (enp_route_destination_t) {
        .network_id = IG_G_8I_NETWORK_ID,
        .node_id = ENP_RELAY_NODE_ID,
    };

    entry.route_sequence = IG_G_8I_ROUTE_SEQUENCE;
    entry.expires_at_ms =
        enp_context_time_ms(s_runtime.context) +
        IG_G_8I_ROUTE_LIFETIME_MS;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    if (!enp_route_metric_init(
            &entry.metric,
            ENP_ROUTE_METRIC_HOP_COUNT)) {
        ESP_LOGE(
            TAG,
            "Cannot install test route: metric initialization failed");
        return false;
    }

    entry.metric.value = IG_G_8I_ROUTE_METRIC;
    entry.metric.valid = true;

    const enp_route_entry_t *const existing =
        enp_route_table_lookup_const(
            routes,
            entry.destination);

    if (existing != NULL) {
        return enp_route_table_update(routes, &entry);
    }

    for (size_t i = 0U; i < routes->count; ++i) {
        if ((routes->entries[i].destination.network_id ==
                 entry.destination.network_id) &&
            (routes->entries[i].destination.node_id ==
                 entry.destination.node_id)) {
            return enp_route_table_update(routes, &entry);
        }
    }

    return enp_route_table_insert(routes, &entry);
}

static bool ig_g_8i_route_is_ready(
    const enp_routing_data_path_t *path,
    enp_route_destination_t *next_hop)
{
    if ((path == NULL) || (next_hop == NULL)) {
        return false;
    }

    const enp_address_t destination = {
        .network = IG_G_8I_NETWORK_ID,
        .node = ENP_SENSOR_NODE_ID,
    };

    if (!enp_routing_data_path_get_next_hop(
            path,
            &destination,
            next_hop)) {
        return false;
    }

    return (next_hop->network_id == IG_G_8I_NETWORK_ID) &&
           (next_hop->node_id == ENP_RELAY_NODE_ID);
}

static bool ig_g_8i_prepare_route(void)
{
    if (!ig_g_8i_neighbour_is_active(
            s_runtime.context,
            ENP_RELAY_NODE_ID)) {
        ESP_LOGW(
            TAG,
            "Cannot prepare test route: Relay node 2 is not ACTIVE");
        return false;
    }

    if (!ig_g_8i_install_test_route()) {
        ESP_LOGE(
            TAG,
            "Failed to install test route 1/3 -> 1/2");
        return false;
    }

    enp_route_destination_t next_hop = {0};

    if (!ig_g_8i_route_is_ready(
            s_runtime.routing_path,
            &next_hop)) {
        ESP_LOGE(
            TAG,
            "Test route verification FAILED: "
            "destination=1/3 is not via 1/2");
        return false;
    }

    ESP_LOGI(
        TAG,
        "Test route READY: destination=1/3 next-hop=1/2");

    return true;
}

static bool ig_g_8i_make_application_packet(
    enp_context_t *context,
    enp_packet_t *packet,
    unsigned packet_number)
{
    if ((context == NULL) || (packet == NULL)) {
        return false;
    }

    const enp_address_t source = {
        .network = context->network.id,
        .node = context->network.local.id,
    };

    enp_packet_init(
        packet,
        ENP_PACKET_APPLICATION,
        &source);

    enp_header_t *const header = enp_packet_header(packet);

    if (header == NULL) {
        return false;
    }

    header->destination.network = IG_G_8I_NETWORK_ID;
    header->destination.node = ENP_SENSOR_NODE_ID;

    /*
     * Use the existing ENP originator sequence allocator.
     * This is the same mechanism used by the established IG-F.7.8
     * application packet generator.
     */
    header->sequence =
        context->network.local.next_sequence++;

    char payload[32];

    const int payload_length = snprintf(
        payload,
        sizeof(payload),
        "IG-G.8i-TEST-%u",
        packet_number);

    if ((payload_length < 0) ||
        ((size_t)payload_length >= sizeof(payload))) {
        return false;
    }

    void *const payload_data = enp_packet_payload(packet);

    if (payload_data == NULL) {
        return false;
    }

    memcpy(
        payload_data,
        payload,
        (size_t)payload_length);

    return (enp_packet_seal(
                packet,
                (uint16_t)payload_length) == ESP_OK);
}

static bool ig_g_8i_send_one(
    enp_context_t *context,
    enp_routing_data_path_t *path,
    unsigned packet_number)
{
    if ((context == NULL) || (path == NULL)) {
        return false;
    }

    if (!ig_g_8i_prepare_route()) {
        ESP_LOGE(
            TAG,
            "DATA #%u: required route 1/3 via 1/2 unavailable",
            packet_number);
        return false;
    }

    enp_packet_t packet = {0};

    if (!ig_g_8i_make_application_packet(
            context,
            &packet,
            packet_number)) {
        ESP_LOGE(
            TAG,
            "DATA #%u: packet construction/sealing failed",
            packet_number);
        return false;
    }

    const enp_header_t *const header =
        enp_packet_header_const(&packet);

    if (header == NULL) {
        ESP_LOGE(
            TAG,
            "DATA #%u: packet header unavailable",
            packet_number);
        return false;
    }

    ESP_LOGI(
        TAG,
        "DATA #%u: sequence=%u destination=%u/%u next-hop=1/2",
        packet_number,
        (unsigned)header->sequence,
        (unsigned)header->destination.network,
        (unsigned)header->destination.node);

    const esp_err_t result =
        enp_routing_data_path_submit(
            path,
            &packet);

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "DATA #%u: submit result=%s",
            packet_number,
            esp_err_to_name(result));
        return false;
    }

    ESP_LOGI(
        TAG,
        "DATA #%u: submit accepted",
        packet_number);

    return true;
}

static bool ig_g_8i_wait_for_topology(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i TEST TOPOLOGY ADMISSION");
    ESP_LOGI(TAG, "Gateway=1 Relay=2 Sensor=3");
    ESP_LOGI(TAG, "Waiting for Relay node 2 and Sensor node 3");
    ESP_LOGI(TAG, "======================================");

    for (;;) {
        const bool relay_active =
            ig_g_8i_neighbour_is_active(
                s_runtime.context,
                ENP_RELAY_NODE_ID);

        const bool sensor_active =
            ig_g_8i_neighbour_is_active(
                s_runtime.context,
                ENP_SENSOR_NODE_ID);

        if (relay_active && sensor_active) {
            ESP_LOGI(
                TAG,
                "Topology admitted: Relay=ACTIVE Sensor=ACTIVE");
            return true;
        }

        vTaskDelay(
            pdMS_TO_TICKS(IG_G_8I_ROUTE_POLL_MS));
    }
}

static bool ig_g_8i_run_positive_control(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i POSITIVE CONTROL");
    ESP_LOGI(TAG, "Controlled route: 1/3 via 1/2");
    ESP_LOGI(
        TAG,
        "Sending %u application DATA packets",
        IG_G_8I_POSITIVE_PACKETS);
    ESP_LOGI(TAG, "======================================");

    for (unsigned i = 0U;
         i < IG_G_8I_POSITIVE_PACKETS;
         ++i) {

        ++s_runtime.packet_number;

        /*
         * A positive-control submit failure is logged, but does not cause
         * the test task to manufacture or repair a route.
         */
        (void)ig_g_8i_send_one(
            s_runtime.context,
            s_runtime.routing_path,
            s_runtime.packet_number);

        if (i + 1U < IG_G_8I_POSITIVE_PACKETS) {
            vTaskDelay(
                pdMS_TO_TICKS(IG_G_8I_PACKET_INTERVAL_MS));
        }
    }

    ESP_LOGI(TAG, "IG-G.8i POSITIVE CONTROL COMPLETE");

    return true;
}

static void ig_g_8i_operator_window(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i FAILURE PHASE ARMED");
    ESP_LOGI(TAG, "POWER OFF NODE 3 NOW.");
    ESP_LOGI(
        TAG,
        "Failure DATA begins in %u ms",
        IG_G_8I_OPERATOR_WINDOW_MS);
    ESP_LOGI(TAG, "No console command is required.");
    ESP_LOGI(TAG, "======================================");

    vTaskDelay(
        pdMS_TO_TICKS(IG_G_8I_OPERATOR_WINDOW_MS));
}

static void ig_g_8i_send_failure_traffic(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i FAILURE TRAFFIC");
    ESP_LOGI(TAG, "Node 3 is expected to be OFF");
    ESP_LOGI(TAG, "Controlled route: 1/3 via 1/2");
    ESP_LOGI(TAG, "======================================");

    for (unsigned i = 0U;
         i < IG_G_8I_FAILURE_PACKETS;
         ++i) {

        ++s_runtime.packet_number;

        /*
         * The failure result is intentionally not converted into a test
         * failure. Once the production route/failure machinery reacts,
         * subsequent submissions may legitimately return NOT_FOUND.
         *
         * The important observation is what the production routing stack
         * does with the first failed transmission.
         */
        (void)ig_g_8i_send_one(
            s_runtime.context,
            s_runtime.routing_path,
            s_runtime.packet_number);

        if (i + 1U < IG_G_8I_FAILURE_PACKETS) {
            vTaskDelay(
                pdMS_TO_TICKS(IG_G_8I_PACKET_INTERVAL_MS));
        }
    }

    ESP_LOGI(TAG, "IG-G.8i FAILURE TRAFFIC COMPLETE");
}

static void ig_g_8i_diag_task(void *argument)
{
    (void)argument;

    const enp_route_failure_coalescer_t *const coalescer =
        enp_routing_runtime_failure_coalescer();
    enp_route_repair_adapter_t *const adapter =
        enp_routing_runtime_repair_adapter();
    enp_route_repair_t *const repair =
        (adapter != NULL) ? adapter->repair : NULL;

    if ((coalescer == NULL) || (repair == NULL)) {
        ESP_LOGE(TAG, "IG-G.8i diagnostic: runtime components unavailable");
        vTaskDelete(NULL);
        return;
    }

    enp_route_failure_coalescer_stats_t baseline = {0};
    if (!enp_route_failure_coalescer_get_stats(coalescer, &baseline)) {
        ESP_LOGE(TAG, "IG-G.8i diagnostic: IG-D baseline unavailable");
        vTaskDelete(NULL);
        return;
    }

    const uint32_t baseline_requests =
        enp_route_repair_request_count(repair);
    const uint32_t baseline_consumed =
        enp_route_repair_consumed_count(repair);

    ESP_LOGI(TAG, "IG-G.8i DIAG BASELINE node=%u",
             (unsigned)s_runtime.context->network.local.id);
    ESP_LOGI(TAG,
             "IG-D observed=%u accepted=%u",
             (unsigned)baseline.observed_count,
             (unsigned)baseline.accepted_count);
    ESP_LOGI(TAG,
             "E5D requests=%u consumed=%u",
             (unsigned)baseline_requests,
             (unsigned)baseline_consumed);

    const TickType_t poll_ticks = pdMS_TO_TICKS(IG_G_8I_DIAG_POLL_MS);
    const TickType_t window_ticks = pdMS_TO_TICKS(IG_G_8I_DIAG_WINDOW_MS);
    TickType_t elapsed = 0;

    while (elapsed < window_ticks) {
        vTaskDelay(poll_ticks);
        elapsed += poll_ticks;

        enp_route_failure_coalescer_stats_t current = {0};
        if (!enp_route_failure_coalescer_get_stats(coalescer, &current)) {
            break;
        }

        const uint32_t requests = enp_route_repair_request_count(repair);
        const uint32_t consumed = enp_route_repair_consumed_count(repair);

        if ((current.observed_count != baseline.observed_count) ||
            (current.accepted_count != baseline.accepted_count) ||
            (requests != baseline_requests) ||
            (consumed != baseline_consumed)) {
            ESP_LOGI(TAG, "IG-G.8i DIAG CHANGE DETECTED");
            ESP_LOGI(TAG,
                     "IG-D observed=%u (+%u) accepted=%u (+%u)",
                     (unsigned)current.observed_count,
                     (unsigned)(current.observed_count - baseline.observed_count),
                     (unsigned)current.accepted_count,
                     (unsigned)(current.accepted_count - baseline.accepted_count));
            ESP_LOGI(TAG,
                     "E5D requests=%u (+%u) consumed=%u (+%u)",
                     (unsigned)requests,
                     (unsigned)(requests - baseline_requests),
                     (unsigned)consumed,
                     (unsigned)(consumed - baseline_consumed));
            baseline = current;
        }
    }

    enp_route_failure_coalescer_stats_t final = {0};
    (void)enp_route_failure_coalescer_get_stats(coalescer, &final);
    const uint32_t final_requests = enp_route_repair_request_count(repair);
    const uint32_t final_consumed = enp_route_repair_consumed_count(repair);

    ESP_LOGI(TAG, "IG-G.8i DIAG FINAL");
    ESP_LOGI(TAG,
             "IG-D observed=%u accepted=%u",
             (unsigned)final.observed_count,
             (unsigned)final.accepted_count);
    ESP_LOGI(TAG,
             "E5D requests=%u consumed=%u",
             (unsigned)final_requests,
             (unsigned)final_consumed);

    vTaskDelete(NULL);
}

static void ig_g_8i_task(void *argument)
{
    (void)argument;

    if (!ig_g_8i_is_gateway(s_runtime.context)) {
        ESP_LOGI(
            TAG,
            "Inactive on node=%u; gateway is node=%u",
            (s_runtime.context != NULL)
                ? (unsigned)s_runtime.context->network.local.id
                : 0U,
            (unsigned)ENP_GATEWAY_NODE_ID);

        vTaskDelete(NULL);
        return;
    }

    s_runtime.routing_path =
        enp_routing_runtime_data_path();

    if (s_runtime.routing_path == NULL) {
        ESP_LOGE(TAG, "Routing data path unavailable");
        vTaskDelete(NULL);
        return;
    }

    if (!ig_g_8i_wait_for_topology()) {
        ESP_LOGE(TAG, "Topology admission failed");
        vTaskDelete(NULL);
        return;
    }

    if (!ig_g_8i_prepare_route()) {
        ESP_LOGE(
            TAG,
            "Initial controlled route installation failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i THREE-NODE TEST");
    ESP_LOGI(TAG, "Gateway=1 Relay=2 Sensor=3");
    ESP_LOGI(TAG, "Controlled route: 1/3 via 1/2");
    ESP_LOGI(TAG, "======================================");

    if (!ig_g_8i_run_positive_control()) {
        ESP_LOGE(TAG, "Positive control failed");
        vTaskDelete(NULL);
        return;
    }

    ig_g_8i_operator_window();

    ig_g_8i_send_failure_traffic();

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "IG-G.8i TEST HARNESS COMPLETE");
    ESP_LOGI(
        TAG,
        "Total DATA packets generated=%u",
        s_runtime.packet_number);
    ESP_LOGI(TAG, "Inspect Node 1 and Node 2 logs for:");
    ESP_LOGI(TAG, "transport failure / E5C admission failure");
    ESP_LOGI(TAG, "extended route-failure callback");
    ESP_LOGI(TAG, "IG-D coalescing");
    ESP_LOGI(TAG, "E5D repair admission");
    ESP_LOGI(TAG, "======================================");

    vTaskDelete(NULL);
}

void ig_g_8i_test_start(enp_context_t *context)
{
    if (context == NULL) {
        ESP_LOGE(TAG, "Cannot start: NULL ENP context");
        return;
    }

    memset(&s_runtime, 0, sizeof(s_runtime));
    s_runtime.context = context;

    if (context->network.local.id == ENP_RELAY_NODE_ID) {
        TaskHandle_t const diag_task =
            xTaskCreateStatic(
                ig_g_8i_diag_task,
                "ig_g8i_diag",
                IG_G_8I_DIAG_TASK_STACK_WORDS,
                NULL,
                IG_G_8I_TASK_PRIORITY,
                s_diag_task_stack,
                &s_diag_task_control);

        if (diag_task == NULL) {
            ESP_LOGE(TAG, "Failed to create IG-G.8i diagnostic task");
        } else {
            ESP_LOGI(TAG, "IG-G.8i diagnostic task started on relay node");
        }
    }

    TaskHandle_t const task =
        xTaskCreateStatic(
            ig_g_8i_task,
            "ig_g8i_test",
            IG_G_8I_TASK_STACK_WORDS,
            NULL,
            IG_G_8I_TASK_PRIORITY,
            s_task_stack,
            &s_task_control);

    if (task == NULL) {
        ESP_LOGE(
            TAG,
            "Failed to create IG-G.8i test task");
    } else {
        ESP_LOGI(
            TAG,
            "IG-G.8i test task started");
    }
}
