/*
 * E3.2.4_test_enp_neighbor_identity_main_RELAY_ROLE.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.2.4 real three-node neighbor identity consistency test.
  *
  * Roles:
  *   Node 1 = Gateway
  *   Node 2 = Relay
  *   Node 3 = Sensor
  *
  * ESP-IDF target: 6.0.2
  *
  * E3.2.4 verifies that periodic discovery announcements refresh existing
  * neighbor entries without changing the logical-node-to-transport identity.
  * The test uses only fields actually present in enp_neighbor_t: logical
  * address, transport address, role, state, and last_seen_ms.
  * The periodic announcements remain enabled; repeated refreshes are silent.
  */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_err.h"
 #include "esp_event.h"
 #include "esp_log.h"
 #include "esp_netif.h"
 #include "esp_now.h"
 #include "nvs_flash.h"

 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 #include "config/enp_config.h"
 #include "core/enp_context.h"
 #include "core/enp_transport.h"
 #include "core/protocol/enp_packet.h"
 #include "core/service/discovery/enp_discovery.h"
 #include "core/service/discovery/enp_service_discovery.h"
 #include "link/enp_transport_espnow.h"
 #include "link/enp_transport_wifi.h"

 #ifndef CONFIG_ENP_E3_NODE_ID
 #define CONFIG_ENP_E3_NODE_ID 1
 #endif

 #define E3_NETWORK_ID                 1U
 #define E3_NODE_A                    1U
 #define E3_NODE_B                    2U
 #define E3_NODE_C                    3U
 #define E3_EXPECTED_NEIGHBORS        2U
 #define E3_ANNOUNCE_PERIOD_MS        2000U
 #define E3_STABILITY_WINDOW_MS       5500U
 #define E3_TASK_STACK_SIZE           4096U
 #define E3_TASK_PRIORITY             4U

 static const char *TAG = "E3_2";

 static enp_context_t s_context;

 static StaticTask_t s_control_task_buffer;
 static StackType_t s_control_task_stack[E3_TASK_STACK_SIZE];
 static TaskHandle_t s_control_task;

 static StaticTask_t s_announce_task_buffer;
 static StackType_t s_announce_task_stack[E3_TASK_STACK_SIZE];
 static TaskHandle_t s_announce_task;

 static uint32_t s_discovery_rx_count;
 static uint32_t s_neighbor_created_count;
 static uint32_t s_neighbor_refresh_count;
 static bool s_test_running;
 static bool s_test_complete;
 static bool s_test_failed;
 static uint32_t s_test_start_ms;
 static uint32_t s_baseline_last_seen[E3_EXPECTED_NEIGHBORS];
 static uint8_t s_baseline_transport[E3_EXPECTED_NEIGHBORS][ESP_NOW_ETH_ALEN];
 static enp_role_t s_baseline_role[E3_EXPECTED_NEIGHBORS];

 static uint16_t expected_neighbor_node(size_t index)
 {
     const uint16_t self = (uint16_t)CONFIG_ENP_E3_NODE_ID;
     uint16_t nodes[E3_EXPECTED_NEIGHBORS];
     size_t count = 0U;

     for (uint16_t node = E3_NODE_A; node <= E3_NODE_C; ++node) {
         if (node != self) {
             nodes[count++] = node;
         }
     }

     return index < count ? nodes[index] : 0U;
 }

 static const enp_neighbor_t *find_expected_neighbor(uint16_t node_id)
 {
     const enp_address_t address = {
         .network = E3_NETWORK_ID,
         .node = node_id
     };

     return enp_neighbor_find_const(&s_context.neighbors, &address);
 }

 static bool validate_neighbor_table(bool require_refresh)
 {
     if (enp_neighbor_count(&s_context.neighbors) != E3_EXPECTED_NEIGHBORS) {
         return false;
     }

     for (size_t index = 0U; index < E3_EXPECTED_NEIGHBORS; ++index) {
         const uint16_t node_id = expected_neighbor_node(index);
         const enp_neighbor_t *neighbor = find_expected_neighbor(node_id);

         if (neighbor == NULL ||
             neighbor->state != ENP_NEIGHBOR_STATE_ACTIVE ||
             neighbor->address.network != E3_NETWORK_ID ||
             neighbor->transport_address.length != ESP_NOW_ETH_ALEN) {
             return false;
         }

         if (require_refresh &&
             neighbor->last_seen_ms <= s_baseline_last_seen[index]) {
             return false;
         }

         if (memcmp(neighbor->transport_address.value,
                    s_baseline_transport[index],
                    ESP_NOW_ETH_ALEN) != 0) {
             return false;
         }

         if (neighbor->role != s_baseline_role[index]) {
             return false;
         }
     }

     return true;
 }

 static void begin_stability_test(void)
 {
     if (s_test_running || s_test_complete) {
         return;
     }

     if (enp_neighbor_count(&s_context.neighbors) != E3_EXPECTED_NEIGHBORS) {
         return;
     }

     for (size_t index = 0U; index < E3_EXPECTED_NEIGHBORS; ++index) {
         const enp_neighbor_t *neighbor =
                 find_expected_neighbor(expected_neighbor_node(index));

         if (neighbor == NULL) {
             return;
         }

         s_baseline_last_seen[index] = neighbor->last_seen_ms;
         memcpy(s_baseline_transport[index],
                neighbor->transport_address.value,
                ESP_NOW_ETH_ALEN);
         s_baseline_role[index] = neighbor->role;
     }

     s_test_running = true;
     s_test_start_ms = enp_context_time_ms(&s_context);

     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "E3.2.4 neighbor identity consistency window started");
     ESP_LOGI(TAG, "Expected active neighbors: %u",
              (unsigned)E3_EXPECTED_NEIGHBORS);
     ESP_LOGI(TAG, "Announcements continue every %u ms; refreshes are silent",
              (unsigned)E3_ANNOUNCE_PERIOD_MS);
 }

 static void finish_stability_test(void)
 {
     if (!s_test_running || s_test_complete) {
         return;
     }

     const uint32_t now = enp_context_time_ms(&s_context);

     if ((now - s_test_start_ms) < E3_STABILITY_WINDOW_MS) {
         return;
     }

     const bool table_ok = validate_neighbor_table(true);
     const bool refresh_ok = s_neighbor_refresh_count >= E3_EXPECTED_NEIGHBORS;
     const bool create_ok = s_neighbor_created_count == E3_EXPECTED_NEIGHBORS;

     ESP_LOGI(TAG, "--------------------------------------");
     ESP_LOGI(TAG, "E3.2.4 neighbor identity consistency results");
     ESP_LOGI(TAG, "Discovery announcements RX: %lu",
              (unsigned long)s_discovery_rx_count);
     ESP_LOGI(TAG, "Neighbor entries created:   %lu",
              (unsigned long)s_neighbor_created_count);
     ESP_LOGI(TAG, "Neighbor entries refreshed: %lu",
              (unsigned long)s_neighbor_refresh_count);
     ESP_LOGI(TAG, "Active neighbor count:      %u",
              (unsigned)enp_neighbor_count(&s_context.neighbors));

     if (!create_ok) {
         ESP_LOGE(TAG, "FAIL: expected exactly 2 neighbor entries to be created");
         s_test_failed = true;
     } else {
         ESP_LOGI(TAG, "PASS: exactly 2 neighbor entries were created");
     }

     if (!refresh_ok) {
         ESP_LOGE(TAG, "FAIL: existing neighbors were not refreshed");
         s_test_failed = true;
     } else {
         ESP_LOGI(TAG, "PASS: existing neighbor entries were refreshed");
     }

     if (!table_ok) {
         ESP_LOGE(TAG, "FAIL: neighbor identity or table state changed unexpectedly");
         s_test_failed = true;
     } else {
         ESP_LOGI(TAG, "PASS: logical node -> transport identity remains stable");
         ESP_LOGI(TAG, "PASS: neighbor role identity remains stable");
         ESP_LOGI(TAG, "PASS: neighbor last_seen_ms advanced for existing entries");
     }

     s_test_complete = true;
     s_test_running = false;

     ESP_LOGI(TAG, "======================================");
     if (s_test_failed) {
         ESP_LOGE(TAG, "E3.2.4 NEIGHBOR IDENTITY TEST FAILED");
     } else {
         ESP_LOGI(TAG, "ALL E3.2.4 NEIGHBOR IDENTITY TESTS PASSED");
     }
     ESP_LOGI(TAG, "======================================");
 }

 static void enp_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     if (source == NULL || data == NULL ||
         length < ENP_HEADER_SIZE + ENP_CRC_SIZE ||
         length > sizeof(enp_packet_t)) {
         return;
     }

     enp_packet_t packet;
     memset(&packet, 0, sizeof(packet));
     memcpy(enp_packet_data(&packet), data, length);

     if (!enp_packet_verify(&packet)) {
         ESP_LOGW(TAG, "Rejected invalid ENP frame");
         return;
     }

     const enp_header_t *header = enp_packet_header_const(&packet);
     if (header == NULL || header->type != ENP_PACKET_DISCOVERY) {
         return;
     }

     if (header->payload_length != ENP_DISCOVERY_PAYLOAD_SIZE) {
         return;
     }

     const enp_discovery_payload_t *discovery =
             (const enp_discovery_payload_t *)enp_packet_payload_const(&packet);

     if (discovery->reserved != 0U ||
         enp_address_is_broadcast(&header->source) ||
         header->source.network != E3_NETWORK_ID ||
         header->source.node == CONFIG_ENP_E3_NODE_ID) {
         return;
     }

     const enp_neighbor_t *existing =
             enp_neighbor_find_const(&s_context.neighbors, &header->source);

     const esp_err_t err = enp_neighbor_update(
             &s_context.neighbors,
             &header->source,
             source,
             (enp_role_t)discovery->role,
             discovery->capabilities,
             header->sequence,
             0,
             enp_context_time_ms(&s_context));

     if (err != ESP_OK) {
         ESP_LOGW(TAG, "Neighbor update failed: %s", esp_err_to_name(err));
         s_test_failed = true;
         return;
     }

     ++s_discovery_rx_count;

     if (existing == NULL) {
         ++s_neighbor_created_count;
         ESP_LOGI(TAG,
                  "Neighbor discovered: node=%lu transport-len=%u",
                  (unsigned long)header->source.node,
                  (unsigned)source->length);
     } else {
         ++s_neighbor_refresh_count;
         /* Deliberately silent: this is an existing neighbor refresh. */
     }

     begin_stability_test();
 }

 static void neighbor_announce_task(void *argument)
 {
     (void)argument;

     for (;;) {
         (void)enp_service_discovery_send(&s_context);
         vTaskDelay(pdMS_TO_TICKS(E3_ANNOUNCE_PERIOD_MS));
     }
 }

 static void control_task(void *argument)
 {
     (void)argument;

     ESP_LOGI(TAG, "READY");
     ESP_LOGI(TAG, "E3.2.4 neighbor identity consistency test starts automatically");

     for (;;) {
         if (s_test_complete) {
             vTaskDelay(pdMS_TO_TICKS(1000));
             continue;
         }

         finish_stability_test();
         vTaskDelay(pdMS_TO_TICKS(100));
     }
 }

 static void nvs_init(void)
 {
     esp_err_t err = nvs_flash_init();

     if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
         err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
         ESP_ERROR_CHECK(nvs_flash_erase());
         err = nvs_flash_init();
     }

     ESP_ERROR_CHECK(err);
 }

 static enp_config_t make_config(void)
 {
     enp_config_t config = {0};

     config.network_id = E3_NETWORK_ID;
     config.node_id = (enp_node_id_t)CONFIG_ENP_E3_NODE_ID;

 #if CONFIG_DEVICE_ROLE_GATEWAY
     config.role = ENP_ROLE_GATEWAY;
 #elif CONFIG_DEVICE_ROLE_RELAY
     config.role = ENP_ROLE_RELAY;
 #elif CONFIG_DEVICE_ROLE_SENSOR
     config.role = ENP_ROLE_SENSOR;
 #else
     config.role = ENP_ROLE_UNKNOWN;
 #endif

     return config;
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "ENP v0.2 E3.2.4 NEIGHBOR IDENTITY");
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "Network: %u", (unsigned)E3_NETWORK_ID);
     ESP_LOGI(TAG, "Node: %u", (unsigned)CONFIG_ENP_E3_NODE_ID);

     if (CONFIG_ENP_E3_NODE_ID == E3_NODE_A) {
         ESP_LOGI(TAG, "Topology role: A / GATEWAY");
     } else if (CONFIG_ENP_E3_NODE_ID == E3_NODE_B) {
         ESP_LOGI(TAG, "Topology role: B / RELAY (DEVICE_ROLE_RELAY)");
     } else if (CONFIG_ENP_E3_NODE_ID == E3_NODE_C) {
         ESP_LOGI(TAG, "Topology role: C / SENSOR (DEVICE_ROLE_SENSOR)");
     } else {
         ESP_LOGE(TAG, "Invalid E3 node ID: %u",
                  (unsigned)CONFIG_ENP_E3_NODE_ID);
         return;
     }

     nvs_init();
     ESP_ERROR_CHECK(esp_netif_init());
     ESP_ERROR_CHECK(esp_event_loop_create_default());
     ESP_ERROR_CHECK(enp_wifi_init());

     while (!enp_wifi_is_connected()) {
         vTaskDelay(pdMS_TO_TICKS(100));
     }

     ESP_LOGI(TAG, "Wi-Fi connected");
     ESP_LOGI(TAG, "Wi-Fi channel: %u", (unsigned)enp_wifi_get_channel());

     enp_config_t config = make_config();
     enp_transport_t *transport = enp_transport_espnow_get();

     if (transport == NULL) {
         ESP_LOGE(TAG, "ESP-NOW transport unavailable");
         return;
     }

     ESP_ERROR_CHECK(enp_context_init(
             &s_context,
             transport,
             &config));

     ESP_ERROR_CHECK(enp_transport_set_receive_callback(
             s_context.transport,
             enp_receive_callback));

     s_announce_task = xTaskCreateStatic(
             neighbor_announce_task,
             "e3_2_announce",
             E3_TASK_STACK_SIZE,
             NULL,
             E3_TASK_PRIORITY,
             s_announce_task_stack,
             &s_announce_task_buffer);

     if (s_announce_task == NULL) {
         ESP_LOGE(TAG, "Failed to create neighbor announcement task");
         return;
     }

     s_control_task = xTaskCreateStatic(
             control_task,
             "e3_2_control",
             E3_TASK_STACK_SIZE,
             NULL,
             E3_TASK_PRIORITY,
             s_control_task_stack,
             &s_control_task_buffer);

     if (s_control_task == NULL) {
         ESP_LOGE(TAG, "Failed to create control task");
         return;
     }

     /* app_main may return; all runtime objects are statically allocated. */
 }