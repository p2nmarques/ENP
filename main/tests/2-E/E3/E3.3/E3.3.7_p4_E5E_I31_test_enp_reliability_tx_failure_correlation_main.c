/*
 * E3.3.7_p4_E5E_I31_test_enp_reliability_tx_failure_correlation_main.c
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5E I31
 * Real Reliability DATA -> E5E correlation -> ESP-NOW TX failure ->
 * Reliability REPAIR_PENDING hardware validation.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Scope:
 *   - real Wi-Fi / ESP-NOW transport
 *   - real Reliability transaction
 *   - real E5E integration boundary
 *   - real asynchronous ESP-NOW TX-result
 *   - E5E correlation resolution
 *   - Reliability REPAIR_PENDING transition
 *
 * This test intentionally stops at the E5E -> repair-request boundary.
 * It does not execute route repair or call repair_result().
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
#include "core/reliability/enp_reliability.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

#define I31_NETWORK_ID 1U
#define I31_LOCAL_NODE 1U
#define I31_DEST_NODE 10U
#define I31_NEXT_HOP_NODE 2U
#define I31_WAIT_MS 10000U
#define I31_POLL_MS 20U
#define I31_UNREACHABLE_MAC {0x02U, 0x00U, 0x00U, 0xE5U, 0x1FU, 0x31U}

static const char *TAG = "E3_3_7_P4_E5E_I31";

static enp_context_t s_context;
static enp_route_table_t s_routes;
static enp_routing_data_path_t s_path;

static volatile bool s_repair_requested;
static volatile unsigned s_repair_request_count;
static volatile enp_reliability_repair_id_t s_repair_id;
static volatile enp_route_destination_t s_repair_destination;
static volatile enp_route_destination_t s_repair_failed_next_hop;

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
        if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(I31_WAIT_MS)) {
            return false;
        }
    }
    return true;
}

static enp_config_t make_config(void) {
    return (enp_config_t){
        .network_id = I31_NETWORK_ID,
        .node_id = I31_LOCAL_NODE,
        .role = ENP_ROLE_GATEWAY,
    };
}

static enp_transport_address_t make_unreachable_address(void) {
    static const uint8_t mac[6] = I31_UNREACHABLE_MAC;
    enp_transport_address_t address = {0};
    address.length = 6U;
    memcpy(address.value, mac, sizeof(mac));
    return address;
}

static bool resolve_transport(void *context,
                              enp_route_destination_t next_hop,
                              enp_transport_address_t *address) {
    (void)context;
    if (address == NULL || next_hop.network_id != I31_NETWORK_ID ||
        next_hop.node_id != I31_NEXT_HOP_NODE) {
        return false;
    }
    *address = make_unreachable_address();
    return true;
}

static enp_route_entry_t make_route(void) {
    enp_route_entry_t route = {0};
    route.destination = (enp_route_destination_t){
        .network_id = I31_NETWORK_ID,
        .node_id = I31_DEST_NODE,
    };
    route.next_hop = (enp_route_destination_t){
        .network_id = I31_NETWORK_ID,
        .node_id = I31_NEXT_HOP_NODE,
    };
    route.metric.type = ENP_ROUTE_METRIC_HOP_COUNT;
    route.metric.value = 1U;
    route.metric.valid = true;
    route.route_sequence = 0xE531U;
    route.expires_at_ms = UINT32_MAX;
    route.state = ENP_ROUTE_STATE_ACTIVE;
    return route;
}

static bool make_data(enp_packet_t *packet) {
    const enp_address_t origin = {
        .network = I31_NETWORK_ID,
        .node = I31_LOCAL_NODE,
    };
    const enp_address_t destination = {
        .network = I31_NETWORK_ID,
        .node = I31_DEST_NODE,
    };
    static const uint8_t payload[] = "E5E-I31-REAL-TX-FAILURE";

    enp_packet_init(packet, ENP_PACKET_APPLICATION, &origin);

    enp_header_t *header = enp_packet_header(packet);
    if (header == NULL) {
        return false;
    }

    header->destination = destination;
    header->flags = ENP_FLAG_ACK_REQUIRED;
    header->sequence = 0xE531U;

    enp_data_header_t *data_header =
        (enp_data_header_t *)enp_packet_payload(packet);
    enp_data_header_init(data_header,
                         ENP_DATA_SUBTYPE_APPLICATION,
                         ENP_DATA_FLAG_NONE,
                         0xE531U,
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

    ++s_repair_request_count;
    s_repair_destination = destination;
    s_repair_failed_next_hop = failed_next_hop;
    s_repair_id = repair_id;
    s_repair_requested = true;

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
    enp_e5e_correlation_id_t correlation_id = ENP_E5E_INVALID_CORRELATION_ID;

    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5E I31");
    ESP_LOGI(TAG, "REAL Reliability -> E5E -> ESP-NOW TX FAILURE");
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
    if (pass) {
        const enp_route_entry_t route = make_route();
        if (!enp_route_table_insert(&s_routes, &route)) {
            ESP_LOGE(TAG, "FAIL: test route installation");
            pass = false;
        }
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

    if (pass && !enp_e5e_integration_init(
                    &s_path, repair_request, NULL)) {
        ESP_LOGE(TAG, "FAIL: E5E integration initialization");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: E5E integration initialized");
    }

    enp_packet_t data;
    if (pass && !make_data(&data)) {
        ESP_LOGE(TAG, "FAIL: test DATA packet construction");
        pass = false;
    }

    if (pass && !enp_reliability_send(&data, 1000U, &handle)) {
        ESP_LOGE(TAG, "FAIL: Reliability DATA submission");
        pass = false;
    }
    if (pass && (handle == ENP_RELIABILITY_INVALID_HANDLE ||
                 !expect_state(handle, ENP_RELIABILITY_STATE_WAITING_FOR_ACK))) {
        ESP_LOGE(TAG, "FAIL: Reliability transaction not WAITING_FOR_ACK");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: Reliability transaction created: handle=%u",
                 (unsigned)handle);
    }

    if (pass && !enp_e5e_get_correlation(handle, &correlation_id)) {
        ESP_LOGE(TAG, "FAIL: E5E correlation was not allocated");
        pass = false;
    }
    if (pass && correlation_id == ENP_E5E_INVALID_CORRELATION_ID) {
        ESP_LOGE(TAG, "FAIL: invalid E5E correlation ID");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: E5E correlation allocated: C=0x%08" PRIX32,
                 correlation_id);
    }

    const TickType_t start = xTaskGetTickCount();
    while (pass && !s_repair_requested &&
           (xTaskGetTickCount() - start) < pdMS_TO_TICKS(I31_WAIT_MS)) {
        enp_reliability_tick(
            (uint32_t)(1000U +
                       ((xTaskGetTickCount() - start) * portTICK_PERIOD_MS)));
        vTaskDelay(pdMS_TO_TICKS(I31_POLL_MS));
    }

    if (pass && !s_repair_requested) {
        ESP_LOGE(TAG, "FAIL: no E5E repair request observed within timeout");
        pass = false;
    }

    if (pass && s_repair_request_count != 1U) {
        ESP_LOGE(TAG, "FAIL: expected exactly one repair request, got %u",
                 s_repair_request_count);
        pass = false;
    }
    if (pass && s_repair_id == ENP_RELIABILITY_INVALID_REPAIR_ID) {
        ESP_LOGE(TAG, "FAIL: invalid repair ID");
        pass = false;
    }
    if (pass && s_repair_destination.node_id != I31_DEST_NODE) {
        ESP_LOGE(TAG, "FAIL: repair destination mismatch");
        pass = false;
    }
    if (pass && s_repair_failed_next_hop.node_id != I31_NEXT_HOP_NODE) {
        ESP_LOGE(TAG, "FAIL: failed next-hop mismatch");
        pass = false;
    }

    if (pass && !expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING)) {
        ESP_LOGE(TAG, "FAIL: Reliability did not enter REPAIR_PENDING");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: correlated TX failure resolved to handle=%u",
                 (unsigned)handle);
        ESP_LOGI(TAG, "PASS: Reliability state = REPAIR_PENDING, repair_id=%u",
                 (unsigned)s_repair_id);
    }

    if (pass && !expect_retry_count(handle, 0U)) {
        ESP_LOGE(TAG, "FAIL: repair consumed retry budget");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: retry_count remains 0 after transport failure");
    }

    for (unsigned i = 0U; pass && i < 3U; ++i) {
        enp_reliability_tick(2000U + (i * 5000U));
    }

    if (pass && (!expect_state(handle, ENP_RELIABILITY_STATE_REPAIR_PENDING) ||
                 !expect_retry_count(handle, 0U))) {
        ESP_LOGE(TAG, "FAIL: REPAIR_PENDING timeout/retry suppression violated");
        pass = false;
    }
    if (pass) {
        ESP_LOGI(TAG, "PASS: timeout/retry remains suppressed during repair");
    }

    if (pass) {
        ESP_LOGI(TAG, "--------------------------------------");
        ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5E I31 hardware validation PASS");
        ESP_LOGI(TAG, "======================================");
    } else {
        ESP_LOGE(TAG, "--------------------------------------");
        ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5E I31 hardware validation FAIL");
        ESP_LOGE(TAG, "======================================");
    }
}
