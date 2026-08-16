/*
 * E3.3.7_p2_test_enp_reliability_dispatcher_integration_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.3.7 Phase 2
 * Reliability <-> dispatcher integration self-test.
 * Target: ESP-IDF 6.0.2
 *
 * Scope:
 *   - reliability transaction creation through the public API
 *   - validated ACK enters through the real ENP dispatcher
 *   - dispatcher ACK service completes the reliability transaction
 *   - dispatcher duplicate suppression prevents duplicate completion
 *   - timeout/retry remains owned by the reliability layer
 *
 * This phase deliberately does NOT integrate route lookup or ESP-NOW
 * transmission. The reliability submit callback remains the boundary to
 * the next ENP routing/transport integration phase.
 */
 
 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_log.h"

 #include "core/enp_context.h"
 #include "core/dispatcher/enp_dispatcher.h"
 #include "core/protocol/enp_packet.h"
 #include "core/protocol/payloads/enp_ack.h"
 #include "core/protocol/payloads/enp_data.h"
 #include "core/reliability/enp_reliability.h"
 #include "core/reliability/enp_reliability_service.h"

 static const char *TAG = "E3_3_7";

 #define TEST_NETWORK_ID        1U
 #define TEST_ORIGIN_NODE      1U
 #define TEST_DEST_NODE        3U
 #define TEST_DATA_SEQUENCE    0x7101U
 #define TEST_APP_SEQUENCE     0x0051U
 #define TEST_ACK_SEQUENCE     0x9101U
 #define TEST_PAYLOAD_TEXT     "E3.3.7-PHASE2"

 static unsigned s_submit_count;
 static unsigned s_result_count;
 static enp_reliability_result_t s_last_result;
 static enp_packet_t s_last_submitted;

 static esp_err_t submit_stub(
         const enp_packet_t *packet,
         void *user_context)
 {
     (void)user_context;

     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     ++s_submit_count;
     s_last_submitted = *packet;
     return ESP_OK;
 }

 static void result_callback(
         enp_reliability_handle_t handle,
         enp_reliability_result_t result,
         void *user_context)
 {
     (void)handle;
     (void)user_context;
     ++s_result_count;
     s_last_result = result;
 }

 static bool make_data_variant(enp_packet_t *packet, enp_sequence_t data_sequence, uint32_t application_sequence)
 {
     if (packet == NULL)
     {
         return false;
     }

     const enp_address_t origin = {
         .network = TEST_NETWORK_ID,
         .node = TEST_ORIGIN_NODE,
     };

     const enp_address_t destination = {
         .network = TEST_NETWORK_ID,
         .node = TEST_DEST_NODE,
     };

     static const uint8_t payload[] = TEST_PAYLOAD_TEXT;

     enp_packet_init(packet, ENP_PACKET_APPLICATION, &origin);

     enp_header_t *header = enp_packet_header(packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = destination;
     header->flags = ENP_FLAG_ACK_REQUIRED;
     header->sequence = data_sequence;
     header->ttl = ENP_DEFAULT_TTL;

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

 static bool make_data(enp_packet_t *packet)
 {
     return make_data_variant(packet, TEST_DATA_SEQUENCE, TEST_APP_SEQUENCE);
 }

 static bool make_ack_variant(enp_packet_t *packet, enp_sequence_t data_sequence, uint32_t application_sequence, enp_sequence_t ack_sequence)
 {
     if (packet == NULL)
     {
         return false;
     }

     const enp_address_t origin = {
         .network = TEST_NETWORK_ID,
         .node = TEST_DEST_NODE,
     };

     const enp_address_t destination = {
         .network = TEST_NETWORK_ID,
         .node = TEST_ORIGIN_NODE,
     };

     enp_packet_init(packet, ENP_PACKET_ACK, &origin);

     enp_header_t *header = enp_packet_header(packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = destination;
     header->sequence = ack_sequence;
     header->ttl = ENP_DEFAULT_TTL;

     enp_ack_payload_t *ack =
             (enp_ack_payload_t *)enp_packet_payload(packet);

     enp_ack_payload_init(
             ack,
             data_sequence,
             application_sequence);

     return enp_packet_seal(packet, ENP_ACK_WIRE_SIZE) == ESP_OK;
 }

 static bool make_ack(enp_packet_t *packet)
 {
     return make_ack_variant(packet, TEST_DATA_SEQUENCE, TEST_APP_SEQUENCE, TEST_ACK_SEQUENCE);
 }

 static bool expect(bool condition, const char *description)
 {
     if (condition)
     {
         ESP_LOGI(TAG, "PASS: %s", description);
         return true;
     }

     ESP_LOGE(TAG, "FAIL: %s", description);
     return false;
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.3.7 PHASE 2 DISPATCHER INTEGRATION");
     ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG, "======================================");

     enp_context_t context;
     memset(&context, 0, sizeof(context));

     enp_packet_t data;
     enp_packet_t ack;
     enp_transport_address_t transport_source;
     memset(&transport_source, 0, sizeof(transport_source));

     bool pass = true;

     s_submit_count = 0U;
     s_result_count = 0U;
     s_last_result = ENP_RELIABILITY_RESULT_NONE;
     memset(&s_last_submitted, 0, sizeof(s_last_submitted));

     pass &= expect(
             enp_dispatcher_init(&context) == ESP_OK,
             "dispatcher initialized");

     pass &= expect(
             enp_reliability_init(),
             "reliability initialized");

     pass &= expect(
             enp_reliability_set_submit_callback(submit_stub, NULL),
             "reliability submit boundary configured");

     pass &= expect(
             enp_reliability_set_result_callback(result_callback, NULL),
             "reliability result callback configured");

     pass &= expect(
             enp_reliability_start(),
             "reliability started");

     pass &= expect(
             enp_dispatcher_register(enp_reliability_service_get()) == ESP_OK,
             "reliability ACK service registered with dispatcher");

     pass &= expect(
             make_data(&data),
             "reliable DATA packet constructed");

     enp_reliability_handle_t handle = ENP_RELIABILITY_INVALID_HANDLE;

     pass &= expect(
             enp_reliability_send(&data, 1000U, &handle),
             "reliable DATA submitted through reliability API");

     pass &= expect(
             s_submit_count == 1U,
             "initial DATA submission count is exactly 1");

     pass &= expect(
             memcmp(&data, &s_last_submitted, sizeof(data)) == 0,
             "initial submission preserves complete DATA packet");

     pass &= expect(
             make_ack(&ack),
             "correlated ACK packet constructed");

     pass &= expect(
             enp_dispatcher_dispatch(&ack, &transport_source) == ESP_OK,
             "ACK entered through ENP dispatcher");

     pass &= expect(
             s_result_count == 1U &&
             s_last_result == ENP_RELIABILITY_RESULT_DELIVERED,
             "dispatcher-delivered ACK completed reliability transaction");

     /*
      * The dispatcher owns the duplicate cache. Re-dispatching the exact
      * same ACK must therefore not call the reliability service again.
      */
     pass &= expect(
             enp_dispatcher_dispatch(&ack, &transport_source) == ESP_OK,
             "duplicate ACK accepted by dispatcher as suppressed traffic");

     pass &= expect(
             s_result_count == 1U,
             "duplicate ACK did not produce duplicate completion");

     /* New transaction: prove timeout/retry remains reliability-owned. */
     s_submit_count = 0U;
     s_result_count = 0U;
     s_last_result = ENP_RELIABILITY_RESULT_NONE;
     memset(&s_last_submitted, 0, sizeof(s_last_submitted));

     enp_packet_t retry_data;
     enp_packet_t retry_ack;
     const enp_sequence_t retry_data_sequence =
             (enp_sequence_t)(TEST_DATA_SEQUENCE + 1U);
     const uint32_t retry_application_sequence =
             TEST_APP_SEQUENCE + 1U;
     const enp_sequence_t retry_ack_sequence =
             (enp_sequence_t)(TEST_ACK_SEQUENCE + 1U);

     pass &= expect(
             make_data_variant(
                     &retry_data,
                     retry_data_sequence,
                     retry_application_sequence),
             "second reliable DATA packet constructed with a new transaction identity");

     enp_reliability_handle_t retry_handle = ENP_RELIABILITY_INVALID_HANDLE;

     pass &= expect(
             enp_reliability_send(&retry_data, 5000U, &retry_handle),
             "second reliable DATA transaction created");

     enp_reliability_tick(5999U);

     pass &= expect(
             s_submit_count == 1U,
             "no retry before ACK timeout");

     enp_reliability_tick(6000U);

     pass &= expect(
             s_submit_count == 2U,
             "reliability layer retransmitted exactly once after timeout");

     pass &= expect(
             memcmp(&retry_data, &s_last_submitted, sizeof(retry_data)) == 0,
             "retransmission preserves identical DATA transaction identity");

     pass &= expect(
             make_ack_variant(
                     &retry_ack,
                     retry_data_sequence,
                     retry_application_sequence,
                     retry_ack_sequence),
             "retry transaction correlated ACK constructed with a new ACK identity");

     pass &= expect(
             enp_dispatcher_dispatch(&retry_ack, &transport_source) == ESP_OK,
             "retry transaction ACK entered through dispatcher");

     pass &= expect(
             s_result_count == 1U &&
             s_last_result == ENP_RELIABILITY_RESULT_DELIVERED,
             "retry transaction completed through dispatcher ACK path");

     enp_reliability_deinit();
     (void)enp_dispatcher_deinit();

     ESP_LOGI(TAG, "--------------------------------------");
     if (pass)
     {
         ESP_LOGI(TAG, "E3.3.7 Phase 2 self-test PASS");
     }
     else
     {
         ESP_LOGE(TAG, "E3.3.7 Phase 2 self-test FAIL");
     }
     ESP_LOGI(TAG, "======================================");
 }
