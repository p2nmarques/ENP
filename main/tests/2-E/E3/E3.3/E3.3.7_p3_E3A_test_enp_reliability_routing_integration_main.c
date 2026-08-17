/*
 * E3.3.7_p3_E3_test_enp_reliability_routing_integration_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7 Phase 3 / E3
 * Reliability -> Routing Data Path integration self-test.
 *
 * Target: ESP-IDF 6.0.2
 *
 * This test intentionally uses the existing reliability submit callback
 * as the integration boundary. No transport-specific code is added to
 * the reliability layer and no new production API is required.
 */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_err.h"
 #include "esp_log.h"

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

 static const char *TAG = "E3_3_7_E3";

 #define TEST_NETWORK_ID        1U
 #define TEST_ORIGIN_NODE_ID   1U
 #define TEST_DEST_NODE_ID     3U
 #define TEST_NEXT_HOP_B       2U
 #define TEST_NEXT_HOP_E       5U
 #define TEST_DATA_SEQUENCE    0x7801U
 #define TEST_APP_SEQUENCE     0x0042U
 #define TEST_ACK_SEQUENCE     0x9801U
 #define TEST_TTL              8U

 /* Large ENP runtime objects remain in static storage. */
 static enp_context_t s_context;
 static enp_route_table_t s_routes;
 static enp_routing_data_path_t s_routing_data_path;
 static enp_packet_t s_packet;
 static enp_packet_t s_first_submission;
 static enp_packet_t s_retry_submission;
 static enp_packet_t s_ack_packet;

 static enp_transport_address_t s_last_destination;
 static unsigned s_transport_init_calls;
 static unsigned s_transport_deinit_calls;
 static unsigned s_transport_send_calls;
 static unsigned s_transport_callback_calls;
 static enp_transport_receive_callback_t s_receive_callback;

 static unsigned s_result_count;
 static enp_reliability_result_t s_last_result;

 static enp_transport_address_t make_transport_address(uint8_t seed)
 {
     enp_transport_address_t address = {
         .length = 6U
     };

     for (size_t i = 0U; i < address.length; ++i)
     {
         address.value[i] = (uint8_t)(seed + i);
     }

     return address;
 }

 static bool transport_address_equal(
         const enp_transport_address_t *left,
         const enp_transport_address_t *right)
 {
     if ((left == NULL) || (right == NULL) ||
         (left->length != right->length))
     {
         return false;
     }

     return memcmp(left->value, right->value, left->length) == 0;
 }

 static void mock_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     (void)source;
     (void)data;
     (void)length;
 }

 static esp_err_t mock_transport_init(const enp_config_t *config)
 {
     ++s_transport_init_calls;
     return (config != NULL) ? ESP_OK : ESP_ERR_INVALID_ARG;
 }

 static esp_err_t mock_transport_deinit(void)
 {
     ++s_transport_deinit_calls;
     return ESP_OK;
 }

 static esp_err_t mock_transport_send(
         const enp_transport_address_t *destination,
         const void *data,
         size_t length)
 {
     if ((destination == NULL) ||
         (data == NULL) ||
         (length == 0U) ||
         (length > sizeof(enp_packet_t)))
     {
         return ESP_ERR_INVALID_ARG;
     }

     ++s_transport_send_calls;
     s_last_destination = *destination;

     if (s_transport_send_calls == 1U)
     {
         memset(&s_first_submission, 0, sizeof(s_first_submission));
         memcpy(enp_packet_data(&s_first_submission), data, length);
     }
     else if (s_transport_send_calls == 2U)
     {
         memset(&s_retry_submission, 0, sizeof(s_retry_submission));
         memcpy(enp_packet_data(&s_retry_submission), data, length);
     }

     return ESP_OK;
 }

 static esp_err_t mock_transport_set_receive_callback(
         enp_transport_receive_callback_t callback)
 {
     if (callback == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     ++s_transport_callback_calls;
     s_receive_callback = callback;
     return ESP_OK;
 }

 static enp_transport_t s_mock_transport = {
     .init = mock_transport_init,
     .deinit = mock_transport_deinit,
     .send = mock_transport_send,
     .set_receive_callback = mock_transport_set_receive_callback,
 };

 static enp_config_t make_config(void)
 {
     return (enp_config_t){
         .network_id = TEST_NETWORK_ID,
         .node_id = TEST_ORIGIN_NODE_ID,
         .role = ENP_ROLE_GATEWAY,
     };
 }

 static enp_address_t make_address(uint32_t node_id)
 {
     return (enp_address_t){
         .network = TEST_NETWORK_ID,
         .node = node_id,
     };
 }

 static bool install_route(
         uint32_t next_hop,
         uint32_t route_sequence)
 {
     const enp_route_entry_t entry = {
         .destination = {
             .network_id = TEST_NETWORK_ID,
             .node_id = TEST_DEST_NODE_ID,
         },
         .next_hop = {
             .network_id = TEST_NETWORK_ID,
             .node_id = next_hop,
         },
         .metric = {
             .valid = true,
             .type = ENP_ROUTE_METRIC_HOP_COUNT,
             .value = 2U,
         },
         .route_sequence = route_sequence,
         .expires_at_ms = UINT32_MAX,
         .state = ENP_ROUTE_STATE_ACTIVE,
     };

     if (route_sequence == 1U)
     {
         return enp_route_table_insert(&s_routes, &entry);
     }

     return enp_route_table_update(&s_routes, &entry);
 }

 static bool install_neighbor(
         uint32_t node_id,
         uint8_t transport_seed)
 {
     const enp_address_t address = make_address(node_id);
     const enp_transport_address_t transport_address =
             make_transport_address(transport_seed);

     return enp_neighbor_update(
                    &s_context.neighbors,
                    &address,
                    &transport_address,
                    ENP_ROLE_RELAY,
                    0U,
                    1U,
                    0,
                    0U) == ESP_OK;
 }

 static bool make_data_packet(
         enp_packet_t *packet,
         enp_sequence_t data_sequence,
         uint32_t application_sequence)
 {
     if (packet == NULL)
     {
         return false;
     }

     const enp_address_t origin = make_address(TEST_ORIGIN_NODE_ID);
     const enp_address_t destination = make_address(TEST_DEST_NODE_ID);

     static const uint8_t payload[] = {
         0xE3U, 0x37U, 0x03U, 0x01U
     };

     enp_packet_init(
             packet,
             ENP_PACKET_APPLICATION,
             &origin);

     enp_header_t *header = enp_packet_header(packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = destination;
     header->flags = ENP_FLAG_ACK_REQUIRED;
     header->ttl = TEST_TTL;
     header->sequence = data_sequence;

     enp_data_header_t *data_header =
             (enp_data_header_t *)enp_packet_payload(packet);

     enp_data_header_init(
             data_header,
             ENP_DATA_SUBTYPE_APPLICATION,
             ENP_DATA_FLAG_NONE,
             application_sequence,
             (uint16_t)sizeof(payload));

     memcpy(
             (uint8_t *)data_header + ENP_DATA_HEADER_SIZE,
             payload,
             sizeof(payload));

     return enp_packet_seal(
                    packet,
                    (uint16_t)(ENP_DATA_HEADER_SIZE + sizeof(payload))) == ESP_OK;
 }

 static bool make_ack_packet(
         enp_packet_t *packet,
         enp_sequence_t data_sequence,
         uint32_t application_sequence,
         enp_sequence_t ack_sequence)
 {
     if (packet == NULL)
     {
         return false;
     }

     const enp_address_t origin = make_address(TEST_DEST_NODE_ID);
     const enp_address_t destination = make_address(TEST_ORIGIN_NODE_ID);

     enp_packet_init(
             packet,
             ENP_PACKET_ACK,
             &origin);

     enp_header_t *header = enp_packet_header(packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = destination;
     header->sequence = ack_sequence;

     enp_ack_payload_t *ack =
             (enp_ack_payload_t *)enp_packet_payload(packet);

     enp_ack_payload_init(
             ack,
             data_sequence,
             application_sequence);

     return enp_packet_seal(
                    packet,
                    ENP_ACK_WIRE_SIZE) == ESP_OK;
 }

 /* Reliability -> routing integration adapter. */
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

     ++s_result_count;
     s_last_result = result;
 }

 static bool packets_equal(
         const enp_packet_t *left,
         const enp_packet_t *right)
 {
     if ((left == NULL) || (right == NULL))
     {
         return false;
     }

     return memcmp(
                    enp_packet_data_const(left),
                    enp_packet_data_const(right),
                    sizeof(enp_packet_t)) == 0;
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

     const enp_address_t logical_address = {
         .network = next_hop.network_id,
         .node = next_hop.node_id,
     };

     return enp_neighbor_get_transport_address(
                    &enp_context->neighbors,
                    &logical_address,
                    transport_address) == ESP_OK;
 }

 static bool check_result(
         bool condition,
         const char *message,
         bool *pass)
 {
     if (condition)
     {
         ESP_LOGI(TAG, "PASS: %s", message);
         return true;
     }

     ESP_LOGE(TAG, "FAIL: %s", message);
     if (pass != NULL)
     {
         *pass = false;
     }

     return false;
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.3.7 PHASE 3 / E3 RELIABILITY -> ROUTING");
     ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG, "======================================");

     bool pass = true;
     memset(&s_context, 0, sizeof(s_context));
     memset(&s_routes, 0, sizeof(s_routes));
     memset(&s_routing_data_path, 0, sizeof(s_routing_data_path));
     memset(&s_packet, 0, sizeof(s_packet));
     memset(&s_first_submission, 0, sizeof(s_first_submission));
     memset(&s_retry_submission, 0, sizeof(s_retry_submission));
     memset(&s_ack_packet, 0, sizeof(s_ack_packet));
     memset(&s_last_destination, 0, sizeof(s_last_destination));
     s_transport_init_calls = 0U;
     s_transport_deinit_calls = 0U;
     s_transport_send_calls = 0U;
     s_transport_callback_calls = 0U;
     s_receive_callback = NULL;
     s_result_count = 0U;
     s_last_result = ENP_RELIABILITY_RESULT_NONE;

     const enp_config_t config = make_config();

     check_result(
             enp_context_init(
                     &s_context,
                     &s_mock_transport,
                     &config) == ESP_OK,
             "ENP context initialized",
             &pass);

     check_result(
             s_transport_init_calls == 1U,
             "controlled transport initialized once",
             &pass);

     check_result(
             enp_transport_set_receive_callback(
                     &s_mock_transport,
                     mock_receive_callback) == ESP_OK &&
             s_transport_callback_calls == 1U &&
             s_receive_callback == mock_receive_callback,
             "controlled transport receive callback registered",
             &pass);

     check_result(
             enp_route_table_init(&s_routes),
             "route table initialized",
             &pass);

     check_result(
             install_neighbor(TEST_NEXT_HOP_B, 0x20U) &&
             install_neighbor(TEST_NEXT_HOP_E, 0x50U),
             "real context neighbor mappings installed",
             &pass);

     check_result(
             install_route(TEST_NEXT_HOP_B, 1U),
             "active route to C initially selects B",
             &pass);

     check_result(
             enp_routing_data_path_init(
                     &s_routing_data_path,
                     &s_routes,
                     s_context.transport,
                     resolve_transport_from_context,
                     &s_context),
             "routing data path initialized",
             &pass);

     /*
      * The existing reliability submit callback is the integration boundary.
      * No reliability API extension and no transport dependency are required.
      */
     check_result(
             enp_reliability_init(),
             "reliability initialized",
             &pass);

     check_result(
             enp_reliability_start(),
             "reliability started",
             &pass);

     check_result(
             enp_reliability_set_submit_callback(
                     reliability_submit_to_routing,
                     &s_routing_data_path),
             "reliability submit callback connected to routing data path",
             &pass);

     check_result(
             enp_reliability_set_result_callback(
                     reliability_result,
                     NULL),
             "reliability result callback configured",
             &pass);

     check_result(
             enp_dispatcher_init(&s_context) == ESP_OK,
             "dispatcher initialized",
             &pass);

     check_result(
             enp_dispatcher_register(
                     enp_reliability_service_get()) == ESP_OK,
             "reliability ACK service registered",
             &pass);

     check_result(
             make_data_packet(
                     &s_packet,
                     TEST_DATA_SEQUENCE,
                     TEST_APP_SEQUENCE),
             "reliable DATA packet constructed",
             &pass);

     enp_reliability_handle_t handle =
             ENP_RELIABILITY_INVALID_HANDLE;

     const unsigned sends_before = s_transport_send_calls;

     check_result(
             enp_reliability_send(
                     &s_packet,
                     1000U,
                     &handle),
             "reliability submitted initial DATA through routing",
             &pass);

     check_result(
             s_transport_send_calls == sends_before + 1U,
             "initial DATA reached controlled transport",
             &pass);

     const enp_transport_address_t expected_b =
             make_transport_address(0x20U);

     check_result(
             transport_address_equal(
                     &s_last_destination,
                     &expected_b),
             "initial route selected B transport address",
             &pass);

     check_result(
             packets_equal(&s_first_submission, &s_packet),
             "initial submission preserved DATA packet identity",
             &pass);

     /*
      * Change only the route. The reliability transaction remains active.
      * The next retry must re-enter routing and use the current next hop.
      */
     check_result(
             install_route(TEST_NEXT_HOP_E, 2U),
             "active route updated to select E",
             &pass);

     enp_reliability_tick(2000U);

     check_result(
             s_transport_send_calls == sends_before + 2U,
             "retransmission reached controlled transport",
             &pass);

     const enp_transport_address_t expected_e =
             make_transport_address(0x50U);

     check_result(
             transport_address_equal(
                     &s_last_destination,
                     &expected_e),
             "retransmission re-ran routing and selected E",
             &pass);

     check_result(
             packets_equal(&s_retry_submission, &s_first_submission),
             "retransmission preserved DATA transaction identity",
             &pass);

     check_result(
             make_ack_packet(
                     &s_ack_packet,
                     TEST_DATA_SEQUENCE,
                     TEST_APP_SEQUENCE,
                     TEST_ACK_SEQUENCE),
             "ACK packet constructed",
             &pass);

     const enp_transport_address_t ack_source =
             make_transport_address(0x70U);

     check_result(
             enp_dispatcher_dispatch(
                     &s_ack_packet,
                     &ack_source) == ESP_OK,
             "ACK passed through dispatcher to reliability service",
             &pass);

     check_result(
             s_result_count == 1U &&
             s_last_result == ENP_RELIABILITY_RESULT_DELIVERED,
             "reliability transaction completed as DELIVERED",
             &pass);

     /* A second ACK must not complete the transaction again. */
     check_result(
             enp_dispatcher_dispatch(
                     &s_ack_packet,
                     &ack_source) == ESP_OK,
             "duplicate ACK safely handled by dispatcher",
             &pass);

     check_result(
             s_result_count == 1U,
             "duplicate ACK did not complete transaction twice",
             &pass);

     check_result(
             enp_dispatcher_deinit() == ESP_OK,
             "dispatcher deinitialized",
             &pass);

     enp_reliability_deinit();

     check_result(
             enp_context_deinit(&s_context) == ESP_OK,
             "ENP context deinitialized",
             &pass);

     check_result(
             s_transport_deinit_calls == 1U,
             "controlled transport deinitialized once",
             &pass);

     ESP_LOGI(TAG, "--------------------------------------");

     if (pass)
     {
         ESP_LOGI(
                 TAG,
                 "E3.3.7 Phase 3 / E3 self-test PASS");
     }
     else
     {
         ESP_LOGE(
                 TAG,
                 "E3.3.7 Phase 3 / E3 self-test FAIL");
     }

     ESP_LOGI(TAG, "======================================");
 }
