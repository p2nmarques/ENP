/*
 * E3.3.7_p3_E3B_test_enp_hardware_espnow_main.c
 *
 * E3.3.7 Phase 3 / E3B hardware integration.
 * Reliability -> Routing Data Path -> real ESP-NOW -> ACK -> Reliability.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Topology:
 *     Node 1 (sender)  <---- ESP-NOW ---->  Node 2 (receiver)
 *
 * The receiver deliberately drops the first ACK. The sender must therefore
 * retransmit the same DATA transaction. The receiver sends the ACK on the
 * second DATA transmission. This validates the real-transport integration
 * of the existing reliability retry path without changing production APIs.
 *
 * Test-specific behavior is deliberately kept in this application. The ENP
 * reliability, routing and transport layers remain unchanged.
 */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <inttypes.h>
 #include <string.h>

 #include "esp_err.h"
 #include "esp_log.h"
 #include "esp_netif.h"
 #include "esp_wifi.h"
 #include "nvs_flash.h"

 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 #include "config/enp_config.h"
 #include "core/enp_context.h"
 #include "core/enp_transport.h"
 #include "core/dispatcher/enp_dispatcher.h"
 #include "core/protocol/enp_packet.h"
 #include "core/protocol/payloads/enp_ack.h"
 #include "core/protocol/payloads/enp_data.h"
 #include "core/reliability/enp_reliability.h"
 #include "core/reliability/enp_reliability_service.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_routing_data_path.h"
 #include "link/enp_transport_espnow.h"
 #include "link/enp_transport_wifi.h"

 static const char *TAG = "E3_3_7_E3_HW";

 #define TEST_NETWORK_ID          ((enp_network_id_t)1U)
 #define TEST_SENDER_NODE_ID     ((enp_node_id_t)1U)
 #define TEST_RECEIVER_NODE_ID   ((enp_node_id_t)2U)
 #define TEST_SEQUENCE            ((enp_sequence_t)0xE301U)
 #define TEST_APP_SEQUENCE        0x00000071U
 #define TEST_ACK_SEQUENCE        ((enp_sequence_t)0xE302U)
 #define TEST_TTL                ((uint8_t)8U)
 #define TEST_PAYLOAD_LENGTH     4U
 #define TEST_RUN_MS             5000U
 #define TEST_POLL_MS            20U
 #define TEST_MAX_DATA_RX        2U

 /* Replace these bytes with Node 2 STA MAC before flashing Node 1. */
 #define E3HW_PEER_MAC_0 0x78U
 #define E3HW_PEER_MAC_1 0x21U
 #define E3HW_PEER_MAC_2 0x84U
 #define E3HW_PEER_MAC_3 0xe6U
 #define E3HW_PEER_MAC_4 0x19U
 #define E3HW_PEER_MAC_5 0x84U

 static enp_context_t s_context;
 static enp_route_table_t s_routes;
 static enp_routing_data_path_t s_routing_path;
 static enp_packet_t s_tx_packet;
 static enp_packet_t s_rx_packet;
 static enp_packet_t s_ack_packet;
 static enp_packet_t s_pending_ack_packet;
 static enp_transport_address_t s_pending_ack_destination;

 /* Receiver-side DATA observation. */
 static volatile bool s_data_seen = false;
 static volatile bool s_data_bad = false;
 static volatile unsigned s_data_rx_count = 0U;
 static volatile bool s_ack_pending = false;
 static volatile bool s_first_ack_dropped = false;
 static volatile bool s_ack_sent = false;

 /* Sender-side ACK observation. */
 static volatile bool s_ack_seen = false;
 static volatile bool s_ack_bad = false;
 static enp_packet_t s_received_ack;

 static volatile unsigned s_result_count = 0U;
 static volatile enp_reliability_result_t s_last_result =
         ENP_RELIABILITY_RESULT_NONE;

 static void nvs_init(void)
 {
     esp_err_t err = nvs_flash_init();
     if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
         (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
     {
         ESP_ERROR_CHECK(nvs_flash_erase());
         err = nvs_flash_init();
     }
     ESP_ERROR_CHECK(err);
 }

 static enp_config_t make_config(enp_node_id_t node_id)
 {
     return (enp_config_t){
         .network_id = TEST_NETWORK_ID,
         .node_id = node_id,
         .role = (node_id == TEST_SENDER_NODE_ID)
                     ? ENP_ROLE_GATEWAY
                     : ENP_ROLE_SENSOR,
     };
 }

 static enp_address_t make_address(enp_node_id_t node_id)
 {
     return (enp_address_t){
         .network = TEST_NETWORK_ID,
         .node = node_id,
     };
 }

 static enp_transport_address_t peer_transport_address(void)
 {
     return (enp_transport_address_t){
         .value = {
             E3HW_PEER_MAC_0, E3HW_PEER_MAC_1, E3HW_PEER_MAC_2,
             E3HW_PEER_MAC_3, E3HW_PEER_MAC_4, E3HW_PEER_MAC_5,
         },
         .length = 6U,
     };
 }

 static bool transport_address_is_zero(
         const enp_transport_address_t *address)
 {
     if ((address == NULL) || (address->length != 6U))
     {
         return true;
     }

     for (size_t i = 0U; i < 6U; ++i)
     {
         if (address->value[i] != 0U)
         {
             return false;
         }
     }
     return true;
 }

 static bool install_sender_route(void)
 {
     const enp_route_entry_t entry = {
         .destination = {
             .network_id = TEST_NETWORK_ID,
             .node_id = TEST_RECEIVER_NODE_ID,
         },
         .next_hop = {
             .network_id = TEST_NETWORK_ID,
             .node_id = TEST_RECEIVER_NODE_ID,
         },
         .metric = {
             .valid = true,
             .type = ENP_ROUTE_METRIC_HOP_COUNT,
             .value = 1U,
         },
         .route_sequence = 1U,
         .expires_at_ms = UINT32_MAX,
         .state = ENP_ROUTE_STATE_ACTIVE,
     };

     return enp_route_table_insert(&s_routes, &entry);
 }

 static bool make_data_packet(void)
 {
     const enp_address_t source = make_address(TEST_SENDER_NODE_ID);

     enp_packet_init(
             &s_tx_packet,
             ENP_PACKET_APPLICATION,
             &source);

     enp_header_t *header = enp_packet_header(&s_tx_packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = make_address(TEST_RECEIVER_NODE_ID);
     header->flags = ENP_FLAG_ACK_REQUIRED;
     header->ttl = TEST_TTL;
     header->sequence = TEST_SEQUENCE;

     static const uint8_t payload[TEST_PAYLOAD_LENGTH] = {
         0xE3U, 0x37U, 0xE3U, 0x07U
     };

     /*
      * Reliability accepts only a structurally valid ENP DATA packet.
      * The DATA payload consists of the 12-byte ENP DATA sub-header
      * followed by the application payload.
      */
     enp_data_header_t *data_header =
             (enp_data_header_t *)enp_packet_payload(&s_tx_packet);

     if (data_header == NULL)
     {
         return false;
     }

     enp_data_header_init(
             data_header,
             ENP_DATA_SUBTYPE_APPLICATION,
             ENP_DATA_FLAG_NONE,
             TEST_APP_SEQUENCE,
             (uint16_t)sizeof(payload));

     memcpy(
             ((uint8_t *)data_header) + ENP_DATA_HEADER_SIZE,
             payload,
             sizeof(payload));

     return enp_packet_seal(
                    &s_tx_packet,
                    (uint16_t)(ENP_DATA_HEADER_SIZE + sizeof(payload))) == ESP_OK;
 }

 static bool make_ack_packet(void)
 {
     const enp_address_t source = make_address(TEST_RECEIVER_NODE_ID);

     enp_packet_init(
             &s_ack_packet,
             ENP_PACKET_ACK,
             &source);

     enp_header_t *header = enp_packet_header(&s_ack_packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = make_address(TEST_SENDER_NODE_ID);
     header->flags = ENP_FLAG_NONE;
     header->ttl = TEST_TTL;
     header->sequence = TEST_ACK_SEQUENCE;

     enp_ack_payload_t *ack =
             (enp_ack_payload_t *)enp_packet_payload(&s_ack_packet);

     enp_ack_payload_init(
             ack,
             TEST_SEQUENCE,
             TEST_APP_SEQUENCE);

     return enp_packet_seal(
                    &s_ack_packet,
                    ENP_ACK_WIRE_SIZE) == ESP_OK;
 }

 static bool resolve_transport_from_context(
         void *context,
         enp_route_destination_t next_hop,
         enp_transport_address_t *transport_address)
 {
     if ((context == NULL) || (transport_address == NULL))
     {
         return false;
     }

     enp_context_t *enp_context = (enp_context_t *)context;

     const enp_address_t logical = {
         .network = next_hop.network_id,
         .node = next_hop.node_id,
     };

     return enp_neighbor_get_transport_address(
                    &enp_context->neighbors,
                    &logical,
                    transport_address) == ESP_OK;
 }

 static esp_err_t reliability_submit_to_routing(
         const enp_packet_t *packet,
         void *user_context)
 {
     enp_routing_data_path_t *path =
             (enp_routing_data_path_t *)user_context;

     if (path == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     return enp_routing_data_path_submit(path, packet);
 }

 static void reliability_result(
         enp_reliability_handle_t handle,
         enp_reliability_result_t result,
         void *user_context)
 {
     (void)handle;
     (void)user_context;

     s_result_count++;
     s_last_result = result;
 }

 static void sender_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     (void)source;

     if ((data == NULL) ||
         (length == 0U) ||
         (length > sizeof(s_received_ack)))
     {
         s_ack_bad = true;
         return;
     }

     memset(&s_received_ack, 0, sizeof(s_received_ack));
     memcpy(enp_packet_data(&s_received_ack), data, length);
     s_ack_seen = true;
 }

 static void receiver_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     if ((source == NULL) ||
         (data == NULL) ||
         (length == 0U) ||
         (length > sizeof(s_rx_packet)))
     {
         s_data_bad = true;
         return;
     }

     memset(&s_rx_packet, 0, sizeof(s_rx_packet));
     memcpy(enp_packet_data(&s_rx_packet), data, length);

     if (!enp_packet_verify(&s_rx_packet))
     {
         ESP_LOGE(TAG, "FAIL: receiver rejected DATA frame integrity");
         s_data_bad = true;
         return;
     }

     const enp_header_t *header =
             enp_packet_header_const(&s_rx_packet);

     const enp_address_t expected_source = make_address(TEST_SENDER_NODE_ID);
     const enp_address_t expected_destination = make_address(TEST_RECEIVER_NODE_ID);

     if ((header == NULL) ||
         (header->type != (uint8_t)ENP_PACKET_APPLICATION) ||
         !enp_address_equal(&header->source, &expected_source) ||
         !enp_address_equal(&header->destination, &expected_destination) ||
         header->sequence != TEST_SEQUENCE ||
         header->flags != ENP_FLAG_ACK_REQUIRED ||
         header->ttl != TEST_TTL ||
         header->payload_length !=
             (uint16_t)(ENP_DATA_HEADER_SIZE + TEST_PAYLOAD_LENGTH))
     {
         ESP_LOGE(TAG, "FAIL: receiver DATA identity/content mismatch");
         s_data_bad = true;
         return;
     }

     const enp_data_header_t *data_header =
             (const enp_data_header_t *)enp_packet_payload_const(&s_rx_packet);

     if ((data_header == NULL) ||
         !enp_data_header_valid(data_header) ||
         !enp_data_payload_length_valid(
                 data_header,
                 TEST_PAYLOAD_LENGTH) ||
         data_header->application_sequence != TEST_APP_SEQUENCE)
     {
         ESP_LOGE(TAG, "FAIL: receiver DATA sub-header validation");
         s_data_bad = true;
         return;
     }

     const uint8_t *application_payload =
             ((const uint8_t *)data_header) + ENP_DATA_HEADER_SIZE;

     const uint8_t expected_payload[TEST_PAYLOAD_LENGTH] = {
         0xE3U, 0x37U, 0xE3U, 0x07U
     };

     if (memcmp(application_payload,
                expected_payload,
                sizeof(expected_payload)) != 0)
     {
         ESP_LOGE(TAG, "FAIL: receiver DATA application payload mismatch");
         s_data_bad = true;
         return;
     }

     ESP_LOGI(TAG,
              "PASS: receiver DATA validated app_seq=0x%08" PRIX32
              " app_payload=%u bytes",
              (uint32_t)data_header->application_sequence,
              (unsigned)TEST_PAYLOAD_LENGTH);

     if (s_data_rx_count < TEST_MAX_DATA_RX)
     {
         ++s_data_rx_count;
     }

     s_data_seen = true;

     if (s_data_rx_count == 1U)
     {
         s_first_ack_dropped = true;
         ESP_LOGI(TAG,
                  "TEST: intentionally dropping first ACK to force retry");
         return;
     }

     if (!s_ack_pending)
     {
         memset(&s_pending_ack_destination, 0,
                sizeof(s_pending_ack_destination));
         s_pending_ack_destination = *source;
         s_ack_pending = true;
     }
 }

 static bool wifi_transport_setup(void)
 {
     nvs_init();

     if (esp_netif_init() != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: esp_netif_init");
         return false;
     }

     if (esp_event_loop_create_default() != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: esp_event_loop_create_default");
         return false;
     }

     if (enp_wifi_init() != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: enp_wifi_init");
         return false;
     }

     while (!enp_wifi_is_connected())
     {
         vTaskDelay(pdMS_TO_TICKS(100));
     }

     ESP_LOGI(TAG,
              "Wi-Fi connected, channel=%u",
              (unsigned)enp_wifi_get_channel());

     uint8_t mac[6] = {0};
     if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK)
     {
         ESP_LOGI(TAG,
                  "STA MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
     }

     return true;
 }

 static bool sender_setup(void)
 {
     const enp_transport_address_t peer = peer_transport_address();
     const enp_address_t receiver = make_address(TEST_RECEIVER_NODE_ID);

     if (transport_address_is_zero(&peer))
     {
         ESP_LOGE(TAG, "FAIL: E3 hardware peer MAC is zero");
         return false;
     }

     enp_transport_t *transport = enp_transport_espnow_get();
     if (transport == NULL)
     {
         ESP_LOGE(TAG, "FAIL: ESP-NOW transport unavailable");
         return false;
     }

     const enp_config_t config = make_config(TEST_SENDER_NODE_ID);

     if (enp_context_init(&s_context, transport, &config) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: sender ENP context initialization");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender ENP context initialized with ESP-NOW");

     if (enp_neighbor_update(
                 &s_context.neighbors,
                 &receiver,
                 &peer,
                 ENP_ROLE_SENSOR,
                 0U,
                 1U,
                 0,
                 enp_context_time_ms(&s_context)) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: sender logical -> physical neighbor mapping");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender Node 2 logical -> ESP-NOW MAC mapping");

     if (!enp_route_table_init(&s_routes) || !install_sender_route())
     {
         ESP_LOGE(TAG, "FAIL: sender route initialization");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender route Node 2 -> next hop Node 2 installed");

     if (!enp_routing_data_path_init(
                 &s_routing_path,
                 &s_routes,
                 s_context.transport,
                 resolve_transport_from_context,
                 &s_context))
     {
         ESP_LOGE(TAG, "FAIL: sender routing data path initialization");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender routing data path initialized");

     if (enp_transport_set_receive_callback(
                 s_context.transport,
                 sender_receive_callback) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: sender ACK receive callback registration");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender ACK receive callback registered");

     if (!enp_reliability_init() || !enp_reliability_start())
     {
         ESP_LOGE(TAG, "FAIL: sender reliability initialization/start");
         return false;
     }

     if (!enp_reliability_set_submit_callback(
                 reliability_submit_to_routing,
                 &s_routing_path) ||
         !enp_reliability_set_result_callback(
                 reliability_result,
                 NULL))
     {
         ESP_LOGE(TAG, "FAIL: sender reliability callback configuration");
         return false;
     }

     ESP_LOGI(TAG,
              "PASS: sender reliability connected to routing data path");

     if (enp_dispatcher_init(&s_context) != ESP_OK ||
         enp_dispatcher_register(
                 enp_reliability_service_get()) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: sender reliability ACK dispatcher setup");
         return false;
     }

     ESP_LOGI(TAG, "PASS: sender reliability ACK dispatcher registered");

     if (!make_data_packet())
     {
         ESP_LOGE(TAG, "FAIL: sender DATA packet construction");
         return false;
     }

     ESP_LOGI(TAG,
              "PASS: sender reliable DATA packet constructed seq=0x%08" PRIX32,
              (uint32_t)TEST_SEQUENCE);

     return true;
 }

 static bool receiver_setup(void)
 {
     enp_transport_t *transport = enp_transport_espnow_get();
     if (transport == NULL)
     {
         ESP_LOGE(TAG, "FAIL: ESP-NOW transport unavailable");
         return false;
     }

     const enp_config_t config = make_config(TEST_RECEIVER_NODE_ID);

     if (enp_context_init(&s_context, transport, &config) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: receiver ENP context initialization");
         return false;
     }

     ESP_LOGI(TAG, "PASS: receiver ENP context initialized with ESP-NOW");

     if (enp_transport_set_receive_callback(
                 s_context.transport,
                 receiver_receive_callback) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: receiver DATA receive callback registration");
         return false;
     }

     ESP_LOGI(TAG, "PASS: receiver DATA receive callback registered");

     return true;
 }

 static void sender_run(void)
 {
     enp_reliability_handle_t handle =
             ENP_RELIABILITY_INVALID_HANDLE;

     const uint32_t start = enp_context_time_ms(&s_context);
     uint32_t now = start;

     if (!enp_reliability_send(&s_tx_packet, now, &handle))
     {
         ESP_LOGE(TAG, "FAIL: reliability initial DATA submission");
         return;
     }

     ESP_LOGI(TAG, "PASS: initial DATA submitted through routing to ESP-NOW");

     while ((enp_context_time_ms(&s_context) - start) < TEST_RUN_MS)
     {
         now = enp_context_time_ms(&s_context);

         /* Reliability state is kept in the application task. The transport
          * callback only copies the ACK; dispatcher processing happens here. */
         if (s_ack_seen)
         {
             s_ack_seen = false;

             if (!enp_packet_verify(&s_received_ack))
             {
                 s_ack_bad = true;
             }
             else
             {
                 const enp_transport_address_t sender_peer = peer_transport_address();
                 if (enp_dispatcher_dispatch(
                              &s_received_ack,
                              &sender_peer) != ESP_OK)
                 {
                     s_ack_bad = true;
                 }
                 else
                 {
                     ESP_LOGI(TAG,
                              "PASS: real ESP-NOW ACK passed through dispatcher");
                 }
             }
         }

         enp_reliability_tick(now);

         if ((s_result_count > 0U) &&
             (s_last_result == ENP_RELIABILITY_RESULT_DELIVERED))
         {
             break;
         }

         vTaskDelay(pdMS_TO_TICKS(TEST_POLL_MS));
     }

     if (s_ack_bad)
     {
         ESP_LOGE(TAG, "FAIL: received ACK validation/dispatch");
         return;
     }

     if (s_result_count != 1U ||
         s_last_result != ENP_RELIABILITY_RESULT_DELIVERED)
     {
         ESP_LOGE(TAG,
                  "FAIL: reliability did not complete after real retry");
         return;
     }

     ESP_LOGI(TAG, "PASS: reliability transaction completed DELIVERED");
 }

 static void receiver_run(void)
 {
     const uint32_t start = enp_context_time_ms(&s_context);
     uint32_t now;

     while ((enp_context_time_ms(&s_context) - start) < TEST_RUN_MS)
     {
         if (s_ack_pending)
         {
             s_ack_pending = false;

             if (!make_ack_packet())
             {
                 ESP_LOGE(TAG, "FAIL: ACK packet construction");
                 break;
             }

             if (enp_transport_send(
                         s_context.transport,
                         &s_pending_ack_destination,
                         enp_packet_data_const(&s_ack_packet),
                         enp_packet_length(&s_ack_packet)) != ESP_OK)
             {
                 ESP_LOGE(TAG, "FAIL: real ESP-NOW ACK transmission");
                 break;
             }

             s_ack_sent = true;
             ESP_LOGI(TAG,
                      "PASS: real ESP-NOW ACK transmitted after retry");
         }

         now = enp_context_time_ms(&s_context);
         (void)now;
         vTaskDelay(pdMS_TO_TICKS(TEST_POLL_MS));

         if (s_ack_sent && s_data_rx_count >= 2U)
         {
             break;
         }
     }

     if (!s_data_seen || s_data_bad)
     {
         ESP_LOGE(TAG, "FAIL: receiver DATA validation");
         return;
     }

     if (s_data_rx_count < 2U)
     {
         ESP_LOGE(TAG,
                  "FAIL: receiver did not observe reliability retransmission");
         return;
     }

     if (!s_first_ack_dropped || !s_ack_sent)
     {
         ESP_LOGE(TAG, "FAIL: receiver ACK-loss/recovery sequence incomplete");
         return;
     }

     ESP_LOGI(TAG,
              "PASS: receiver observed original DATA + one retransmission");
 }

 static bool cleanup(void)
 {
     (void)enp_dispatcher_deinit();
     enp_reliability_deinit();
     return enp_context_deinit(&s_context) == ESP_OK;
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.3.7 PHASE 3 / E3 REAL ESP-NOW HARDWARE");
     ESP_LOGI(TAG, "Reliability -> Routing -> ESP-NOW -> ACK");
     ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG, "======================================");

     if (!wifi_transport_setup())
     {
         ESP_LOGE(TAG, "E3 hardware test aborted during Wi-Fi setup");
         return;
     }

 #if CONFIG_ENP_E3_NODE_ID == 1
     ESP_LOGI(TAG, "Role: NODE 1 / reliability sender");

     if (!sender_setup())
     {
         ESP_LOGE(TAG, "E3 hardware sender setup FAIL");
         (void)cleanup();
         return;
     }

     sender_run();

     const bool pass =
             (s_result_count == 1U) &&
             (s_last_result == ENP_RELIABILITY_RESULT_DELIVERED) &&
             !s_ack_bad;

     (void)cleanup();

     ESP_LOGI(TAG, "--------------------------------------");
     if (pass)
     {
         ESP_LOGI(TAG,
                  "E3.3.7 Phase 3 / E3 hardware sender PASS");
     }
     else
     {
         ESP_LOGE(TAG,
                  "E3.3.7 Phase 3 / E3 hardware sender FAIL");
     }
     ESP_LOGI(TAG, "======================================");
 #else
     ESP_LOGI(TAG, "Role: NODE 2 / ACK receiver");

     if (!receiver_setup())
     {
         ESP_LOGE(TAG, "E3 hardware receiver setup FAIL");
         (void)cleanup();
         return;
     }

     receiver_run();

     const bool pass =
             s_data_seen &&
             !s_data_bad &&
             s_data_rx_count >= 2U &&
             s_first_ack_dropped &&
             s_ack_sent;

     (void)cleanup();

     ESP_LOGI(TAG, "--------------------------------------");
     if (pass)
     {
         ESP_LOGI(TAG,
                  "E3.3.7 Phase 3 / E3 hardware receiver PASS");
     }
     else
     {
         ESP_LOGE(TAG,
                  "E3.3.7 Phase 3 / E3 hardware receiver FAIL");
     }
     ESP_LOGI(TAG, "======================================");
 #endif
 }
