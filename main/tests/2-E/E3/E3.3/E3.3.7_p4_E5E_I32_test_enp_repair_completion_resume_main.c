/*
 * E3.3.7_p4_E5E_I32_test_enp_repair_completion_resume_main.c
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 *
 * E5E-I32: real Reliability -> E5E -> ESP-NOW failure, followed by a
 * successful repair completion and normal Reliability retransmission.
 *
 * Target: ESP-IDF 6.0.2
 * Scope: hardware transport + E5E + Reliability resume semantics.
 * E5D Step-3 is observed only through the existing E5E repair-request
 * boundary; this test does not modify or execute the Step-3 implementation.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config/enp_config.h"
#include "core/enp_context.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_data.h"
#include "core/reliability/enp_e5e.h"
#include "core/reliability/enp_e5e_integration.h"

/* Keep the public observer API visible from the same integration include path
 * used by this test; do not provide a local forward declaration. */
#include "core/reliability/enp_reliability.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

#define I32_NETWORK_ID 1U
#define I32_LOCAL_NODE 1U
#define I32_DEST_NODE 10U
#define I32_NEXT_HOP_NODE 2U
#define I32_WAIT_MS 10000U
#define I32_POLL_MS 20U
#define I32_UNREACHABLE_MAC {0x02U, 0x00U, 0x00U, 0xE5U, 0x1FU, 0x32U}

static const char *TAG = "E3_3_7_P4_E5E_I32";

static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_routing_data_path_t s_path;

static volatile bool s_repair_requested;
static volatile unsigned s_repair_request_count;
static volatile enp_reliability_repair_id_t s_repair_id;
static volatile unsigned s_correlation_alloc_count;
static volatile enp_e5e_correlation_id_t s_first_allocated_correlation;
static volatile enp_e5e_correlation_id_t s_last_allocated_correlation;
static volatile enp_reliability_handle_t s_last_allocated_handle;

static void correlation_allocated_observer(
    void *context, enp_reliability_handle_t handle,
    enp_e5e_correlation_id_t correlation_id) {
    (void)context;
    ++s_correlation_alloc_count;
    if (s_correlation_alloc_count == 1U) {
        s_first_allocated_correlation = correlation_id;
    }
    s_last_allocated_correlation = correlation_id;
    s_last_allocated_handle = handle;
}

static void nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
        (err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static bool wait_for_wifi(void) {
    const TickType_t start = xTaskGetTickCount();
    while (!enp_wifi_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100U));
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(I32_WAIT_MS)) {
            return false;
        }
    }
    return true;
}

static enp_config_t make_config(void) {
    return (enp_config_t){
        .network_id = I32_NETWORK_ID,
        .node_id = I32_LOCAL_NODE,
        .role = ENP_ROLE_GATEWAY,
    };
}

static enp_transport_address_t make_unreachable_address(void) {
    static const uint8_t mac[6] = I32_UNREACHABLE_MAC;
    enp_transport_address_t address = {0};
    address.length = 6U;
    memcpy(address.value, mac, sizeof(mac));
    return address;
}

static bool resolve_transport(void *context,
                              enp_route_destination_t next_hop,
                              enp_transport_address_t *address) {
    (void)context;
    if (address == NULL || next_hop.network_id != I32_NETWORK_ID ||
        next_hop.node_id != I32_NEXT_HOP_NODE) {
        return false;
    }
    /* The first transmission must fail so that I32 enters REPAIR_PENDING.
     * After the repair request is observed, the simulated successful repair
     * makes the next-hop resolution usable again. Using the transport
     * broadcast address (length == 0) gives the retransmission a real ESP-NOW
     * success path without requiring a second hardware node. */
    if (s_repair_requested) {
        memset(address, 0, sizeof(*address));
        return true;
    }

    *address = make_unreachable_address();
    return true;
}

static enp_route_entry_t make_route(void) {
    enp_route_entry_t route = {0};
    route.destination = (enp_route_destination_t){
        .network_id = I32_NETWORK_ID,
        .node_id = I32_DEST_NODE,
    };
    route.next_hop = (enp_route_destination_t){
        .network_id = I32_NETWORK_ID,
        .node_id = I32_NEXT_HOP_NODE,
    };
    route.metric.type = ENP_ROUTE_METRIC_HOP_COUNT;
    route.metric.value = 1U;
    route.metric.valid = true;
    route.route_sequence = 0xE532U;
    route.expires_at_ms = UINT32_MAX;
    route.state = ENP_ROUTE_STATE_ACTIVE;
    return route;
}

static bool make_data(enp_packet_t *packet) {
    const enp_address_t origin = {
        .network = I32_NETWORK_ID,
        .node = I32_LOCAL_NODE,
    };
    const enp_address_t destination = {
        .network = I32_NETWORK_ID,
        .node = I32_DEST_NODE,
    };
    static const uint8_t payload[] = "E5E-I32-REPAIR-RESUME";

    enp_packet_init(packet, ENP_PACKET_APPLICATION, &origin);
    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) {
        return false;
    }

    header->destination = destination;
    header->flags = ENP_FLAG_ACK_REQUIRED;
    header->sequence = 0xE532U;

    enp_data_header_t *data_header =
        (enp_data_header_t *)enp_packet_payload(packet);
    enp_data_header_init(data_header,
                         ENP_DATA_SUBTYPE_APPLICATION,
                         ENP_DATA_FLAG_NONE,
                         0xE532U,
                         (uint16_t)sizeof(payload));

    memcpy((uint8_t *)data_header + ENP_DATA_HEADER_SIZE,
           payload, sizeof(payload));

    return enp_packet_seal(
               packet,
               (uint16_t)(ENP_DATA_HEADER_SIZE + sizeof(payload))) == ESP_OK;
}

static bool repair_request(void *context,
                           enp_route_destination_t destination,
                           enp_route_destination_t failed_next_hop,
                           enp_reliability_repair_id_t repair_id) {
    (void)context;
    if (destination.network_id != I32_NETWORK_ID ||
        destination.node_id != I32_DEST_NODE ||
        failed_next_hop.network_id != I32_NETWORK_ID ||
        failed_next_hop.node_id != I32_NEXT_HOP_NODE ||
        repair_id == ENP_RELIABILITY_INVALID_REPAIR_ID) {
        return false;
    }

    s_repair_requested = true;
    ++s_repair_request_count;
    s_repair_id = repair_id;
    ESP_LOGI(TAG,
             "E5E repair request observed: destination=%u/%u failed_next_hop=%u/%u repair_id=%u",
             destination.network_id, destination.node_id,
             failed_next_hop.network_id, failed_next_hop.node_id,
             (unsigned)repair_id);
    return true;
}

static bool expect_state(enp_reliability_handle_t handle,
                         enp_reliability_state_t expected) {
    enp_reliability_state_t state = ENP_RELIABILITY_STATE_INVALID;
    return enp_reliability_get_state(handle, &state) && state == expected;
}

static bool expect_retry_count(enp_reliability_handle_t handle,
                               uint8_t expected) {
    uint8_t count = 0U;
    return enp_reliability_get_retry_count(handle, &count) && count == expected;
}

void app_main(void) {
    bool pass = true;
    enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;
    enp_e5e_correlation_id_t old_correlation = ENP_E5E_INVALID_CORRELATION_ID;
    enp_e5e_correlation_id_t new_correlation = ENP_E5E_INVALID_CORRELATION_ID;

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5E I32");
    ESP_LOGI(TAG, "REAL REPAIR COMPLETION -> RELIABILITY RESUME");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "======================================");

    nvs_init();
    if (esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: ESP-NETIF initialization");
        pass = false;
    }
    if (pass && esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: default event loop initialization");
        pass = false;
    }
    if (pass && enp_wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: Wi-Fi initialization");
        pass = false;
    }
    if (pass && !wait_for_wifi()) {
        ESP_LOGE(TAG, "FAIL: Wi-Fi connection timeout");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: Wi-Fi connected, channel=%u",
                 (unsigned)enp_wifi_get_channel());
    }

    enp_transport_t *transport = enp_transport_espnow_get();
    const enp_config_t config = make_config();
    if (pass && (transport == NULL ||
                 enp_context_init(&s_context, transport, &config) != ESP_OK)) {
        ESP_LOGE(TAG, "FAIL: ESP-NOW transport initialization");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: real ESP-NOW transport initialized");
    }

    if (pass && !enp_route_table_init(&s_routes)) {
        ESP_LOGE(TAG, "FAIL: route table initialization");
        pass = false;
    }
    if (pass && !enp_route_table_insert(&s_routes, &(const enp_route_entry_t){
            .destination = {I32_NETWORK_ID, I32_DEST_NODE},
            .next_hop = {I32_NETWORK_ID, I32_NEXT_HOP_NODE},
            .metric = {.type = ENP_ROUTE_METRIC_HOP_COUNT, .value = 1U, .valid = true},
            .route_sequence = 0xE532U,
            .expires_at_ms = UINT32_MAX,
            .state = ENP_ROUTE_STATE_ACTIVE})) {
        ESP_LOGE(TAG, "FAIL: route installation");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: active route installed: 1/10 via 1/2");
    }

    if (pass && !enp_routing_data_path_init(
                    &s_path, &s_routes, transport, resolve_transport, NULL)) {
        ESP_LOGE(TAG, "FAIL: routing data path initialization");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: routing data path initialized");
    }

    if (pass && !enp_reliability_init()) {
        ESP_LOGE(TAG, "FAIL: Reliability initialization");
        pass = false;
    }
    if (pass && !enp_reliability_start()) {
        ESP_LOGE(TAG, "FAIL: Reliability start");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: Reliability started");
    }

    if (pass && !enp_e5e_integration_init(&s_path, repair_request, NULL)) {
        ESP_LOGE(TAG, "FAIL: E5E integration initialization");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: E5E integration initialized");
    }
    if (pass && !enp_e5e_set_correlation_allocated_observer(
                       correlation_allocated_observer, NULL)) {
        ESP_LOGE(TAG, "FAIL: E5E correlation allocation observer setup");
        pass = false;
    }

    enp_packet_t data;
    if (pass && !make_data(&data)) {
        ESP_LOGE(TAG, "FAIL: DATA packet construction");
        pass = false;
    }

    if (pass && !enp_reliability_send(&data, 1000U, &handle)) {
        ESP_LOGE(TAG, "FAIL: Reliability DATA submission");
        pass = false;
    }
    if (pass && !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
        ESP_LOGE(TAG, "FAIL: transaction not WAITING_FOR_ACK");
        pass = false;
    }
    if (pass && s_correlation_alloc_count != 1U) {
        ESP_LOGE(TAG, "FAIL: expected one initial correlation allocation, got %u",
                 s_correlation_alloc_count);
        pass = false;
    }
    if (pass) {
        old_correlation = s_first_allocated_correlation;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: H=%u associated with old C=0x%08" PRIX32,
                 (unsigned)handle, old_correlation);
    }

    const TickType_t start = xTaskGetTickCount();
    while (pass && !s_repair_requested &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(I32_WAIT_MS)) {
        enp_reliability_tick(1000U +
                              ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS));
        vTaskDelay(pdMS_TO_TICKS(I32_POLL_MS));
    }

    if (pass && !s_repair_requested) {
        ESP_LOGE(TAG, "FAIL: initial repair request not observed");
        pass = false;
    }
    if (pass && s_repair_request_count != 1U) {
        ESP_LOGE(TAG, "FAIL: expected one repair request, got %u",
                 s_repair_request_count);
        pass = false;
    }
    if (pass && !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING)) {
        ESP_LOGE(TAG, "FAIL: transaction not REPAIR_PENDING");
        pass = false;
    }
    if (pass && !expect_retry_count(handle, 0U)) {
        ESP_LOGE(TAG, "FAIL: retry budget changed during repair");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: H=%u is REPAIR_PENDING, retry_count=0",
                 (unsigned)handle);
    }

    /* Simulate the existing E5D completion boundary. A successful repair
     * must first restore/install the repaired route. The initial transport
     * failure invalidates the route through the existing E5C path; therefore
     * the E5D-success simulation must reactivate that route before E5E asks
     * Reliability to perform the normal retransmission. */
    enp_route_entry_t repaired_route = make_route();
    if (pass && !enp_route_table_update(&s_routes, &repaired_route)) {
        ESP_LOGE(TAG, "FAIL: simulated E5D repair did not reinstall route");
        pass = false;
    }
    if (pass) {
        const enp_route_entry_t *active = enp_route_table_lookup_const(
            &s_routes, repaired_route.destination);
        if (active == NULL || active->state != ENP_ROUTE_STATE_ACTIVE) {
            ESP_LOGE(TAG, "FAIL: simulated E5D repair route is not ACTIVE");
            pass = false;
        }
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: simulated E5D repair reinstalled destination route");
    }

    if (pass && !enp_e5e_on_repair_result(s_repair_id, true, 5000U)) {
        ESP_LOGE(TAG, "FAIL: E5E repair completion was rejected");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: repair completion accepted: repair_id=%u",
                 (unsigned)s_repair_id);
    }

    /* The successful repair synchronously transitions Reliability back to
     * WAITING_FOR_ACK and submits the normal retransmission.  The transport
     * result is asynchronous, so give the ESP-NOW task a short window to
     * settle before asserting the final state.  A successful retransmission
     * must remain WAITING_FOR_ACK; a second repair request indicates that the
     * post-repair transport path failed and is a real test failure. */
    const TickType_t resume_start = xTaskGetTickCount();
    while (pass &&
           (xTaskGetTickCount() - resume_start) < pdMS_TO_TICKS(1000U)) {
        if (expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10U));
    }
    if (pass && !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
        ESP_LOGE(TAG,
                 "FAIL: Reliability did not remain WAITING_FOR_ACK after repair resume");
        pass = false;
    }
    if (pass && s_repair_request_count != 1U) {
        ESP_LOGE(TAG,
                 "FAIL: post-repair retransmission triggered another repair request: %u",
                 s_repair_request_count);
        pass = false;
    }
    if (pass && !expect_retry_count(handle, 1U)) {
        ESP_LOGE(TAG, "FAIL: successful repair did not perform normal retry");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: repair success resumed normal retransmission, retry_count=1");
    }

    if (pass && s_correlation_alloc_count < 2U) {
        ESP_LOGE(TAG, "FAIL: retransmission did not allocate a new correlation");
        pass = false;
    }
    if (pass) {
        new_correlation = s_last_allocated_correlation;
        if (s_last_allocated_handle != handle) {
            ESP_LOGE(TAG, "FAIL: new correlation belongs to wrong handle: H=%u",
                     (unsigned)s_last_allocated_handle);
            pass = false;
        }
    }
    if (pass && new_correlation == old_correlation) {
        ESP_LOGE(TAG, "FAIL: retransmission reused old correlation C=0x%08" PRIX32,
                 old_correlation);
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: retransmission allocated NEW correlation C=0x%08" PRIX32,
                 new_correlation);
    }
    if (pass && s_correlation_alloc_count != 2U) {
        ESP_LOGE(TAG, "FAIL: unexpected correlation allocations after resume: %u",
                 s_correlation_alloc_count);
        pass = false;
    }

    /* The old repair/correlation identity must be stale and harmless. */
    if (pass && enp_e5e_on_repair_result(s_repair_id, true, 5100U)) {
        ESP_LOGE(TAG, "FAIL: duplicate repair completion was accepted");
        pass = false;
    }
    if (pass && enp_e5e_on_transport_result(old_correlation, ESP_FAIL)) {
        ESP_LOGE(TAG, "FAIL: stale old correlation was accepted");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: stale repair completion and old correlation are harmless");
    }

    /* Fresh deadline was installed at 5000 + 1000. */
    enp_reliability_tick(5999U);
    if (pass && !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK)) {
        ESP_LOGE(TAG, "FAIL: transaction left WAITING_FOR_ACK before fresh deadline");
        pass = false;
    }
    if (pass && !expect_retry_count(handle, 1U)) {
        ESP_LOGE(TAG, "FAIL: retry advanced before fresh deadline");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: fresh ACK deadline suppresses retry before t=6000");
    }

    enp_reliability_tick(6000U);
    if (pass && !expect_retry_count(handle, 2U)) {
        ESP_LOGE(TAG, "FAIL: retry did not advance at fresh ACK deadline");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: fresh ACK deadline expired and normal retry_count=2 occurred");
    }

    if (pass) {
        ESP_LOGI(TAG, "--------------------------------------");
        ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5E I32 hardware validation PASS");
        ESP_LOGI(TAG, "======================================");
    } else {
        ESP_LOGE(TAG, "--------------------------------------");
        ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5E I32 hardware validation FAIL");
        ESP_LOGE(TAG, "======================================");
    }
}
