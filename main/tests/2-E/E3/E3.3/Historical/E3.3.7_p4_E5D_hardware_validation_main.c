/*
 * E3.3.7_p4_E5D_hardware_validation_main.c
 *
 *  Created on: Aug 22, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5D
 * Step-3 hardware validation — real E5C -> E5D -> R4 repair.
 *
 * Scope:
 *   Validate the frozen Step-3 adapter on real ESP-NOW hardware without
 *   changing the adapter implementation.
 *
 * Hardware topology for this first hardware gate:
 *
 *   Node 1 (A/GATEWAY) ---- Node 3 (C/DESTINATION)
 *          |
 *          +---- Node 2 (B/FAILED NEXT-HOP)
 *
 * The initial route A -> C is deliberately installed through B. After the
 * route is established, Node 2 is powered off. A then submits real DATA to C.
 * E5C observes the real ESP-NOW TX failure, invalidates the A->C route and
 * emits the frozen E5D repair request. Step-3 starts R4 discovery and the
 * alternate direct A->C path is rediscovered using Node 3 as next-hop.
 *
 * This is intentionally a direct-alternative hardware gate. It validates
 * the complete real E5C -> E5D -> R4 orchestration without introducing a
 * new multi-hop relay requirement into the frozen Step-3 adapter.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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
#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_data.h"
#include "core/protocol/payloads/enp_routing.h"
#include "core/network/enp_neighbor.h"
#include "core/routing/enp_route_repair.h"
#include "core/routing/enp_route_repair_adapter.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_route_metric.h"
#include "core/routing/enp_routing_data_path.h"
#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"
#include "core/service/discovery/enp_discovery.h"
#include "core/service/discovery/enp_service_discovery.h"

#ifndef CONFIG_ENP_E3_NODE_ID
#define CONFIG_ENP_E3_NODE_ID 1
#endif

#define E5D_NETWORK_ID          1U
#define E5D_NODE_A              1U
#define E5D_NODE_B              2U
#define E5D_NODE_C              3U
#define E5D_ROUTE_SEQUENCE      7U
#define E5D_WAIT_MS             15000U
#define E5D_TASK_STACK_SIZE     4096U
#define E5D_TASK_PRIORITY       5U

static const char *TAG = "E3_3_7_P4_E5D";

static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_route_repair_t s_repair;
static enp_route_repair_adapter_t s_adapter;
static enp_routing_data_path_t s_data_path;
static enp_transport_t *s_transport;

static StaticTask_t s_announce_task_control;
static StackType_t s_announce_task_stack[E5D_TASK_STACK_SIZE];
static StaticTask_t s_tick_task_control;
static StackType_t s_tick_task_stack[E5D_TASK_STACK_SIZE];
static StaticTask_t s_control_task_control;
static StackType_t s_control_task_stack[E5D_TASK_STACK_SIZE];

static bool s_ready;
static bool s_test_failed;
static bool s_fail_command;
static uint32_t s_rreq_rx_count;
static uint32_t s_rrep_rx_count;
static uint32_t s_data_rx_count;

static enp_address_t local_address(void) {
    return (enp_address_t){
        .network = E5D_NETWORK_ID,
        .node = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID};
}

static enp_config_t make_config(void) {
    enp_config_t config = {0};
    config.network_id = E5D_NETWORK_ID;
    config.node_id = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID;
    if (CONFIG_ENP_E3_NODE_ID == E5D_NODE_A) {
        config.role = ENP_ROLE_GATEWAY;
    } else if (CONFIG_ENP_E3_NODE_ID == E5D_NODE_B) {
        config.role = ENP_ROLE_RELAY;
    } else {
        config.role = ENP_ROLE_SENSOR;
    }
    return config;
}

static bool nvs_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return false;
        err = nvs_flash_init();
    }
    return err == ESP_OK;
}

static bool wait_wifi(void) {
    const TickType_t start = xTaskGetTickCount();
    while (!enp_wifi_is_connected()) {
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(E5D_WAIT_MS)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100U));
    }
    return true;
}

static const enp_neighbor_t *find_neighbor(enp_node_id_t node) {
    const enp_address_t address = {.network = E5D_NETWORK_ID, .node = node};
    const enp_neighbor_t *neighbor =
        enp_neighbor_find_const(&s_context.neighbors, &address);
    if (neighbor == NULL || neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE) {
        return NULL;
    }
    return neighbor;
}

static void log_neighbors(void) {
    ESP_LOGI(TAG, "Neighbor table: count=%u",
             (unsigned)s_context.neighbors.count);
    for (size_t i = 0U; i < s_context.neighbors.count; ++i) {
        const enp_neighbor_t *n = &s_context.neighbors.entries[i];
        ESP_LOGI(TAG,
                 "  neighbor[%u]: net=%lu node=%lu state=%u transport-len=%u",
                 (unsigned)i,
                 (unsigned long)n->address.network,
                 (unsigned long)n->address.node,
                 (unsigned)n->state,
                 (unsigned)n->transport_address.length);
    }
}

static bool resolve_transport(void *context,
                              enp_route_destination_t next_hop,
                              enp_transport_address_t *address) {
    (void)context;
    if (address == NULL || next_hop.network_id != E5D_NETWORK_ID) {
        return false;
    }
    const enp_address_t logical = {
        .network = next_hop.network_id, .node = next_hop.node_id};
    return enp_neighbor_get_transport_address(&s_context.neighbors, &logical,
                                               address) == ESP_OK;
}

static bool select_next_hop(void *context,
                            enp_route_destination_t destination,
                            enp_route_destination_t failed_next_hop,
                            enp_route_destination_t *next_hop) {
    (void)context;
    if (next_hop == NULL || destination.network_id != E5D_NETWORK_ID) {
        return false;
    }

    /* First hardware gate: A repairs C through the direct A-C alternative. */
    if (CONFIG_ENP_E3_NODE_ID == E5D_NODE_A && destination.node_id == E5D_NODE_C) {
        if (failed_next_hop.node_id == E5D_NODE_C) return false;
        const enp_neighbor_t *c = find_neighbor(E5D_NODE_C);
        if (c != NULL && c->state == ENP_NEIGHBOR_STATE_ACTIVE) {
            *next_hop = (enp_route_destination_t){
                .network_id = E5D_NETWORK_ID, .node_id = E5D_NODE_C};
            return true;
        }
    }

    return false;
}

static uint32_t now_ms(void *context) {
    (void)context;
    return enp_context_time_ms(&s_context);
}

static void route_failure(void *context,
                          enp_route_destination_t destination,
                          enp_route_destination_t failed_next_hop) {
    (void)context;
    ESP_LOGI(TAG,
             "E5C route failure: destination=%u failed-next-hop=%u -> E5D request",
             (unsigned)destination.node_id,
             (unsigned)failed_next_hop.node_id);
    if (!enp_route_repair_request(&s_repair, destination, failed_next_hop)) {
        ESP_LOGE(TAG, "E5D repair request rejected");
        s_test_failed = true;
    }
}

static bool install_initial_route(void) {
    const enp_neighbor_t *b = find_neighbor(E5D_NODE_B);
    if (b == NULL || b->state != ENP_NEIGHBOR_STATE_ACTIVE) return false;

    enp_route_entry_t entry = {0};
    entry.destination = (enp_route_destination_t){
        .network_id = E5D_NETWORK_ID, .node_id = E5D_NODE_C};
    entry.next_hop = (enp_route_destination_t){
        .network_id = E5D_NETWORK_ID, .node_id = E5D_NODE_B};
    entry.route_sequence = E5D_ROUTE_SEQUENCE;
    entry.expires_at_ms = enp_context_time_ms(&s_context) + 60000U;
    entry.state = ENP_ROUTE_STATE_ACTIVE;
    if (!enp_route_metric_init(&entry.metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
        return false;
    }
    entry.metric.value = 1U;
    entry.metric.valid = true;

    const enp_route_entry_t *existing = enp_route_table_lookup_const(
        &s_routes, entry.destination);
    if (existing != NULL) {
        return enp_route_table_update(&s_routes, &entry);
    }
    for (size_t i = 0U; i < s_routes.count; ++i) {
        if (s_routes.entries[i].destination.node_id == E5D_NODE_C) {
            return enp_route_table_update(&s_routes, &entry);
        }
    }
    return enp_route_table_insert(&s_routes, &entry);
}

static bool make_data_packet(enp_packet_t *packet) {
    if (packet == NULL) return false;
    const enp_address_t source = local_address();
    enp_packet_init(packet, ENP_PACKET_APPLICATION, &source);
    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) return false;
    header->destination.network = E5D_NETWORK_ID;
    header->destination.node = E5D_NODE_C;
    header->sequence = 0xE5D10001U;

    static const uint8_t payload[] = "ENP-P4-E5D-HW";
    memcpy(enp_packet_payload(packet), payload, sizeof(payload) - 1U);
    return enp_packet_seal(packet, sizeof(payload) - 1U) == ESP_OK;
}

static bool find_neighbor_logical(const enp_transport_address_t *transport,
                                  enp_address_t *logical) {
    if (transport == NULL || logical == NULL) return false;
    for (size_t i = 0U; i < s_context.neighbors.count; ++i) {
        const enp_neighbor_t *n = &s_context.neighbors.entries[i];
        if (n->state != ENP_NEIGHBOR_STATE_ACTIVE) continue;
        if (n->transport_address.length == transport->length &&
            memcmp(n->transport_address.value, transport->value,
                   transport->length) == 0) {
            *logical = n->address;
            return true;
        }
    }
    return false;
}

static void process_rreq(const enp_packet_t *packet,
                         const enp_transport_address_t *source) {
    const enp_header_t *header = enp_packet_header_const(packet);
    if (header == NULL || enp_packet_payload_const(packet) == NULL) return;
    enp_address_t immediate = {0};
    if (!find_neighbor_logical(source, &immediate)) return;

    enp_routing_rreq_t rreq = {0};
    memcpy(&rreq, enp_packet_payload_const(packet), sizeof(rreq));
    enp_routing_rreq_t forward = {0};
    enp_rreq_result_t result = enp_route_repair_adapter_handle_rreq(
        &s_adapter,
        (enp_rreq_node_t){.network_id = header->source.network,
                          .node_id = header->source.node},
        (enp_rreq_node_t){.network_id = immediate.network,
                          .node_id = immediate.node},
        &rreq, &forward, header->sequence);
    ++s_rreq_rx_count;
    ESP_LOGI(TAG, "RREQ RX: from=%u destination=%u result=%d",
             (unsigned)header->source.node,
             (unsigned)rreq.destination_node_id, (int)result);
}

static void process_rrep(const enp_packet_t *packet,
                         const enp_transport_address_t *source) {
    const enp_header_t *header = enp_packet_header_const(packet);
    if (header == NULL || enp_packet_payload_const(packet) == NULL) return;
    enp_address_t origin = {0};
    if (!find_neighbor_logical(source, &origin)) return;

    enp_routing_rrep_t rrep = {0};
    memcpy(&rrep, enp_packet_payload_const(packet), sizeof(rrep));
    enp_rrep_result_t result = enp_route_repair_adapter_handle_rrep(
        &s_adapter,
        (enp_rrep_node_t){.network_id = origin.network,
                          .node_id = origin.node},
        &rrep, &header->source, header->sequence);
    ++s_rrep_rx_count;
    ESP_LOGI(TAG, "RREP RX: from=%u destination=%u result=%d",
             (unsigned)header->source.node,
             (unsigned)rrep.destination_node_id, (int)result);
}

static void receive_callback(const enp_transport_address_t *source,
                             const void *data, size_t length) {
    if (source == NULL || data == NULL ||
        length < ENP_HEADER_SIZE + ENP_CRC_SIZE ||
        length > sizeof(enp_packet_t)) {
        return;
    }

    enp_packet_t packet = {0};
    memcpy(enp_packet_data(&packet), data, length);
    if (!enp_packet_verify(&packet)) {
        ESP_LOGW(TAG, "Rejected invalid ENP frame: len=%u", (unsigned)length);
        return;
    }

    const enp_header_t *header = enp_packet_header_const(&packet);
    if (header == NULL) return;

    if (header->type == ENP_PACKET_DISCOVERY) {
        if (header->payload_length != ENP_DISCOVERY_PAYLOAD_SIZE) {
            ESP_LOGW(TAG,
                     "Discovery ignored: node=%lu payload=%u expected=%u",
                     (unsigned long)header->source.node,
                     (unsigned)header->payload_length,
                     (unsigned)ENP_DISCOVERY_PAYLOAD_SIZE);
            return;
        }

        const enp_discovery_payload_t *d =
            (const enp_discovery_payload_t *)enp_packet_payload_const(&packet);

        if (d == NULL) {
            ESP_LOGW(TAG, "Discovery ignored: NULL payload");
            return;
        }

        if (d->reserved != 0U || enp_address_is_broadcast(&header->source)) {
            ESP_LOGW(TAG,
                     "Discovery ignored: node=%lu reserved=%u broadcast=%u",
                     (unsigned long)header->source.node,
                     (unsigned)d->reserved,
                     (unsigned)enp_address_is_broadcast(&header->source));
            return;
        }

        const esp_err_t neighbor_err = enp_neighbor_update(
            &s_context.neighbors, &header->source, source,
            (enp_role_t)d->role, d->capabilities, header->sequence, 0,
            enp_context_time_ms(&s_context));

        if (neighbor_err != ESP_OK) {
            ESP_LOGW(TAG,
                     "Neighbor update failed: node=%lu err=%s",
                     (unsigned long)header->source.node,
                     esp_err_to_name(neighbor_err));
            return;
        }

        ESP_LOGI(TAG,
                 "Neighbor ready: node=%lu role=%u transport-len=%u",
                 (unsigned long)header->source.node,
                 (unsigned)d->role,
                 (unsigned)source->length);
        return;
    }

    if (header->type == ENP_PACKET_ROUTE &&
        header->payload_length == ENP_ROUTING_RREQ_WIRE_SIZE) {
        const uint8_t *payload =
            (const uint8_t *)enp_packet_payload_const(&packet);
        if (payload != NULL && payload[1] == ENP_ROUTING_SUBTYPE_RREQ) {
            process_rreq(&packet, source);
        }
        return;
    }

    if (header->type == ENP_PACKET_ROUTE &&
        header->payload_length == ENP_ROUTING_RREP_WIRE_SIZE) {
        const uint8_t *payload =
            (const uint8_t *)enp_packet_payload_const(&packet);
        if (payload != NULL && payload[1] == ENP_ROUTING_SUBTYPE_RREP) {
            process_rrep(&packet, source);
        }
        return;
    }

    if (header->type == ENP_PACKET_APPLICATION) {
        ++s_data_rx_count;
        ESP_LOGI(TAG, "DATA RX: source=%u destination=%u",
                 (unsigned)header->source.node,
                 (unsigned)header->destination.node);
    }
}

static void announce_task(void *arg) {
    (void)arg;
    for (;;) {
        (void)enp_service_discovery_send(&s_context);
        vTaskDelay(pdMS_TO_TICKS(2000U));
    }
}

static void tick_task(void *arg) {
    (void)arg;
    for (;;) {
        (void)enp_route_repair_adapter_tick(&s_adapter,
                                            enp_context_time_ms(&s_context));
        vTaskDelay(pdMS_TO_TICKS(50U));
    }
}

static void check(bool condition, const char *message) {
    if (condition) {
        ESP_LOGI(TAG, "PASS: %s", message);
    } else {
        ESP_LOGE(TAG, "FAIL: %s", message);
        s_test_failed = true;
    }
}

static void run_failure_and_repair(void) {
    check(install_initial_route(),
          "initial route C installed via real failed-next-hop B");

    const enp_route_entry_t *before = enp_route_table_lookup_const(
        &s_routes, (enp_route_destination_t){.network_id = 1U, .node_id = 3U});
    check(before != NULL && before->state == ENP_ROUTE_STATE_ACTIVE,
          "initial route C is ACTIVE");
    check(before != NULL && before->next_hop.node_id == E5D_NODE_B,
          "initial route C uses next-hop B");
    check(before != NULL && before->route_sequence == E5D_ROUTE_SEQUENCE,
          "initial route C sequence is 7");

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "POWER OFF NODE 2 / B NOW");
    ESP_LOGI(TAG, "Then type: e5d fail");
    ESP_LOGI(TAG, "======================================");

    while (!s_fail_command) {
        vTaskDelay(pdMS_TO_TICKS(100U));
    }

    enp_packet_t packet = {0};
    check(make_data_packet(&packet), "real DATA packet constructed and sealed");
    if (!s_test_failed) {
        check(enp_routing_data_path_submit(&s_data_path, &packet) == ESP_OK,
              "real DATA transmission submitted toward failed next-hop B");
    }

    const TickType_t start = xTaskGetTickCount();
    bool saw_stale = false;
    bool saw_repair = false;
    bool repaired = false;

    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(E5D_WAIT_MS)) {
        const enp_route_entry_t *route = enp_route_table_lookup_const(
            &s_routes, (enp_route_destination_t){.network_id = 1U, .node_id = 3U});

        if (route != NULL && route->state == ENP_ROUTE_STATE_STALE) {
            saw_stale = true;
        }
        if (enp_route_repair_adapter_is_active(&s_adapter)) {
            saw_repair = true;
        }
        if (route != NULL && route->state == ENP_ROUTE_STATE_ACTIVE &&
            route->next_hop.node_id == E5D_NODE_C &&
            route->route_sequence == E5D_ROUTE_SEQUENCE) {
            repaired = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50U));
    }

    check(saw_stale, "real E5C failure invalidated route C to STALE");
    check(saw_repair, "real E5C failure created an active E5D repair");
    check(repaired, "real R4 rediscovery repaired route C via next-hop C");

    const enp_route_entry_t *after = enp_route_table_lookup_const(
        &s_routes, (enp_route_destination_t){.network_id = 1U, .node_id = 3U});
    check(after != NULL && after->state == ENP_ROUTE_STATE_ACTIVE,
          "repaired route C is ACTIVE");
    check(after != NULL && after->next_hop.node_id == E5D_NODE_C,
          "repaired route C uses alternative next-hop C");
    check(after != NULL && after->route_sequence == E5D_ROUTE_SEQUENCE,
          "repaired route C preserves sequence 7");

    ESP_LOGI(TAG, "RREQ RX count=%lu RREP RX count=%lu DATA RX count=%lu",
             (unsigned long)s_rreq_rx_count,
             (unsigned long)s_rrep_rx_count,
             (unsigned long)s_data_rx_count);
}

static void control_task(void *arg) {
    (void)arg;
    if (CONFIG_ENP_E3_NODE_ID != E5D_NODE_A) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000U));
    }

    char line[32];
    for (;;) {
        ESP_LOGI(TAG, "Commands: e5d fail | e5d routes");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(500U));
            continue;
        }
        if (strncmp(line, "e5d fail", 8U) == 0) {
            s_fail_command = true;
            ESP_LOGI(TAG, "E5D failure command accepted");
        }
        if (strncmp(line, "e5d routes", 10U) == 0) {
            const enp_route_entry_t *route = enp_route_table_lookup_const(
                &s_routes, (enp_route_destination_t){.network_id = 1U, .node_id = 3U});
            if (route != NULL) {
                ESP_LOGI(TAG, "route C: next=%u seq=%lu state=%u",
                         (unsigned)route->next_hop.node_id,
                         (unsigned long)route->route_sequence,
                         (unsigned)route->state);
            }
        }
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5D");
    ESP_LOGI(TAG, "STEP-3 HARDWARE VALIDATION");
    ESP_LOGI(TAG, "Real E5C -> E5D -> R4 repair");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "Reliability excluded; frozen adapter unchanged");
    ESP_LOGI(TAG, "Node: %u", (unsigned)CONFIG_ENP_E3_NODE_ID);
    ESP_LOGI(TAG, "======================================");

    if (!nvs_init() || esp_netif_init() != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: platform initialization");
        return;
    }
    esp_err_t loop_err = esp_event_loop_create_default();
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "FAIL: event loop initialization");
        return;
    }
    if (enp_wifi_init() != ESP_OK || !wait_wifi()) {
        ESP_LOGE(TAG, "FAIL: Wi-Fi initialization/connection");
        return;
    }

    s_transport = enp_transport_espnow_get();
    if (s_transport == NULL) {
        ESP_LOGE(TAG, "FAIL: ESP-NOW transport unavailable");
        return;
    }

    enp_config_t config = make_config();
    if (enp_context_init(&s_context, s_transport, &config) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: ENP context initialization");
        return;
    }
    if (!enp_route_table_init(&s_routes)) {
        ESP_LOGE(TAG, "FAIL: route table initialization");
        return;
    }
    if (!enp_route_repair_adapter_init(
            &s_adapter, &s_repair, &s_routes, s_transport, local_address(),
            select_next_hop, NULL, resolve_transport, NULL, now_ms, NULL)) {
        ESP_LOGE(TAG, "FAIL: Step-3 adapter initialization");
        return;
    }
    if (!enp_route_repair_init(&s_repair,
                               enp_route_repair_adapter_consume, &s_adapter)) {
        ESP_LOGE(TAG, "FAIL: E5D repair coordinator initialization");
        return;
    }
    if (!enp_routing_data_path_init(&s_data_path, &s_routes, s_transport,
                                    resolve_transport, NULL)) {
        ESP_LOGE(TAG, "FAIL: routing data path initialization");
        return;
    }
    if (!enp_routing_data_path_set_route_failure_callback(
            &s_data_path, route_failure, NULL)) {
        ESP_LOGE(TAG, "FAIL: E5C route-failure callback registration");
        return;
    }
    if (enp_transport_set_receive_callback(s_transport, receive_callback) != ESP_OK) {
        ESP_LOGE(TAG, "FAIL: receive callback registration");
        return;
    }

    (void)xTaskCreateStatic(announce_task, "e5d_announce", E5D_TASK_STACK_SIZE,
                            NULL, E5D_TASK_PRIORITY, s_announce_task_stack,
                            &s_announce_task_control);
    (void)xTaskCreateStatic(tick_task, "e5d_tick", E5D_TASK_STACK_SIZE,
                            NULL, E5D_TASK_PRIORITY, s_tick_task_stack,
                            &s_tick_task_control);
    (void)xTaskCreateStatic(control_task, "e5d_control", E5D_TASK_STACK_SIZE,
                            NULL, E5D_TASK_PRIORITY, s_control_task_stack,
                            &s_control_task_control);

    if (CONFIG_ENP_E3_NODE_ID == E5D_NODE_A) {
        ESP_LOGI(TAG, "Waiting for neighbors B and C...");
        const TickType_t start = xTaskGetTickCount();
        while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(E5D_WAIT_MS)) {
            if (find_neighbor(E5D_NODE_B) != NULL &&
                find_neighbor(E5D_NODE_C) != NULL) {
                s_ready = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100U));
        }
        if (!s_ready) {
            log_neighbors();
            ESP_LOGE(TAG,
                     "FAIL: real neighbors B and C not yet discovered; continuing to wait instead of terminating");
            ESP_LOGI(TAG,
                     "Verify B and C are powered, on the same Wi-Fi channel, and sending discovery");
            for (;;) {
                if (find_neighbor(E5D_NODE_B) != NULL &&
                    find_neighbor(E5D_NODE_C) != NULL) {
                    s_ready = true;
                    ESP_LOGI(TAG, "PASS: real neighbors B and C discovered after extended wait");
                    break;
                }
                vTaskDelay(pdMS_TO_TICKS(500U));
            }
        }

        if (s_ready) {
            run_failure_and_repair();

            if (!s_test_failed) {
                ESP_LOGI(TAG, "--------------------------------------");
                ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5D Step-3 hardware validation PASS");
                ESP_LOGI(TAG, "======================================");
            } else {
                ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5D Step-3 hardware validation FAIL");
            }
        }
    } else {
        ESP_LOGI(TAG, "READY: node participates in P4-E5D hardware validation");
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}
