/*
 * E3.3.7_p4_E2_test_enp_reliability_maintenance_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 */

/**
 * @file E3.3.7_p4_E2_test_enp_reliability_maintenance_main.c
 *
 * @brief E3.3.7 Phase 4 / E2 reliability runtime integration self-test.
 *
 * Target: ESP-IDF 6.0.2
 *
 * This test verifies that the existing ENP maintenance task becomes the
 * periodic scheduler for the transport-independent reliability layer.
 */

 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_err.h"
 #include "esp_log.h"

 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 #include "config/enp_config.h"
 #include "core/enp_context.h"
 #include "core/enp_maintenance.h"
 #include "core/enp_transport.h"
 #include "core/protocol/enp_packet.h"
 #include "core/protocol/payloads/enp_ack.h"
 #include "core/protocol/payloads/enp_data.h"
 #include "core/reliability/enp_reliability.h"

 static const char *TAG = "E3_3_7_P4_E2";

 #define TEST_NETWORK_ID       ((enp_network_id_t)1U)
 #define TEST_ORIGIN_NODE      ((enp_node_id_t)1U)
 #define TEST_DEST_NODE        ((enp_node_id_t)3U)
 #define TEST_DATA_SEQUENCE    ((enp_sequence_t)0xE421U)
 #define TEST_APP_SEQUENCE     ((uint32_t)0x00000052U)
 #define TEST_ACK_SEQUENCE     ((enp_sequence_t)0xA421U)
 #define TEST_PAYLOAD_TEXT     "P4-E2"

 static enp_context_t s_context;
 static enp_transport_t s_transport;
 static enp_packet_t s_data;
 static enp_packet_t s_ack;

 static unsigned s_transport_init_count;
 static unsigned s_transport_deinit_count;
 static unsigned s_transport_send_count;
 static unsigned s_reliability_submit_count;
 static unsigned s_result_count;
 static enp_reliability_result_t s_last_result;
 static enp_packet_t s_last_submitted;

 static esp_err_t transport_init(const enp_config_t *config)
 {
     (void)config;
     ++s_transport_init_count;
     return ESP_OK;
 }

 static esp_err_t transport_deinit(void)
 {
     ++s_transport_deinit_count;
     return ESP_OK;
 }

 static esp_err_t transport_send(
         const enp_transport_address_t *destination,
         const void *data,
         size_t length)
 {
     (void)destination;
     (void)data;
     (void)length;
     ++s_transport_send_count;
     return ESP_OK;
 }

 static esp_err_t transport_set_receive_callback(
         enp_transport_receive_callback_t callback)
 {
     (void)callback;
     return ESP_OK;
 }

 static void make_transport(void)
 {
     memset(&s_transport, 0, sizeof(s_transport));
     s_transport.init = transport_init;
     s_transport.deinit = transport_deinit;
     s_transport.send = transport_send;
     s_transport.set_receive_callback = transport_set_receive_callback;
 }

 static esp_err_t reliability_submit(
         const enp_packet_t *packet,
         void *user_context)
 {
     (void)user_context;

     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     ++s_reliability_submit_count;
     s_last_submitted = *packet;
     return ESP_OK;
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

 static bool make_data(enp_packet_t *packet)
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
     header->sequence = TEST_DATA_SEQUENCE;
     header->ttl = ENP_DEFAULT_TTL;

     enp_data_header_t *data_header =
             (enp_data_header_t *)enp_packet_payload(packet);

     enp_data_header_init(
             data_header,
             ENP_DATA_SUBTYPE_APPLICATION,
             ENP_DATA_FLAG_NONE,
             TEST_APP_SEQUENCE,
             (uint16_t)sizeof(payload));

     memcpy(
             (uint8_t *)data_header + ENP_DATA_HEADER_SIZE,
             payload,
             sizeof(payload));

     return enp_packet_seal(
             packet,
             (uint16_t)(ENP_DATA_HEADER_SIZE + sizeof(payload))) == ESP_OK;
 }

 static bool make_ack(enp_packet_t *packet)
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
     header->sequence = TEST_ACK_SEQUENCE;
     header->ttl = ENP_DEFAULT_TTL;

     enp_ack_payload_t *ack =
             (enp_ack_payload_t *)enp_packet_payload(packet);

     enp_ack_payload_init(
             ack,
             TEST_DATA_SEQUENCE,
             TEST_APP_SEQUENCE);

     return enp_packet_seal(packet, ENP_ACK_WIRE_SIZE) == ESP_OK;
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
     ESP_LOGI(TAG, "E3.3.7 PHASE 4 / E2 RELIABILITY MAINTENANCE");
     ESP_LOGI(TAG, "Reliability tick -> ENP maintenance task");
     ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG, "======================================");

     bool pass = true;

     memset(&s_context, 0, sizeof(s_context));
     memset(&s_data, 0, sizeof(s_data));
     memset(&s_ack, 0, sizeof(s_ack));

     s_transport_init_count = 0U;
     s_transport_deinit_count = 0U;
     s_transport_send_count = 0U;
     s_reliability_submit_count = 0U;
     s_result_count = 0U;
     s_last_result = ENP_RELIABILITY_RESULT_NONE;
     memset(&s_last_submitted, 0, sizeof(s_last_submitted));

     make_transport();

     enp_config_t config;
     memset(&config, 0, sizeof(config));
     config.network_id = TEST_NETWORK_ID;
     config.node_id = TEST_ORIGIN_NODE;

     pass &= expect(
             enp_context_init(&s_context, &s_transport, &config) == ESP_OK,
             "ENP context initialized with controlled transport");

     pass &= expect(
             s_transport_init_count == 1U,
             "controlled transport initialized once");

     pass &= expect(
             enp_reliability_init(),
             "reliability initialized");

     pass &= expect(
             enp_reliability_set_submit_callback(
                     reliability_submit,
                     NULL),
             "reliability submit callback configured");

     pass &= expect(
             enp_reliability_set_result_callback(
                     reliability_result,
                     NULL),
             "reliability result callback configured");

     pass &= expect(
             enp_reliability_start(),
             "reliability started");

     pass &= expect(
             enp_maintenance_init(&s_context) == ESP_OK,
             "ENP maintenance task started");

     pass &= expect(
             make_data(&s_data),
             "reliable DATA packet constructed");

     enp_reliability_handle_t handle =
             ENP_RELIABILITY_INVALID_HANDLE;

     const uint32_t send_time =
             enp_context_time_ms(&s_context);

     pass &= expect(
             enp_reliability_send(
                     &s_data,
                     send_time,
                     &handle),
             "reliable DATA transaction submitted");

     pass &= expect(
             s_reliability_submit_count == 1U,
             "initial reliability submission occurred once");

     /*
      * ENP_DISCOVERY_INTERVAL_MS is 2000 ms in the current baseline and
      * reliability timeout is 1000 ms. The maintenance task therefore
      * invokes enp_reliability_tick() after the transaction has timed out.
      */
     vTaskDelay(pdMS_TO_TICKS(2200U));

     pass &= expect(
             s_reliability_submit_count == 1U,
             "maintenance task caused exactly one reliability retransmission");

     pass &= expect(
             memcmp(
                     &s_data,
                     &s_last_submitted,
                     sizeof(s_data)) == 0,
             "maintenance-driven retransmission preserved DATA transaction identity");

     pass &= expect(
             make_ack(&s_ack),
             "correlated ACK packet constructed");

     pass &= expect(
             enp_reliability_process_ack(
                     &s_ack,
                     enp_context_time_ms(&s_context)),
             "ACK completed reliability transaction");

     pass &= expect(
             s_result_count == 1U &&
             s_last_result == ENP_RELIABILITY_RESULT_DELIVERED,
             "reliability transaction completed as DELIVERED");

     pass &= expect(
             enp_maintenance_deinit() == ESP_OK,
             "ENP maintenance task stop requested");

     /* Allow the static maintenance task to observe the stop notification. */
     vTaskDelay(pdMS_TO_TICKS(100U));

     enp_reliability_deinit();

     pass &= expect(
             enp_context_deinit(&s_context) == ESP_OK,
             "ENP context deinitialized");

     pass &= expect(
             s_transport_deinit_count == 1U,
             "controlled transport deinitialized once");

     ESP_LOGI(TAG, "--------------------------------------");
     if (pass)
     {
         ESP_LOGI(TAG, "E3.3.7 Phase 4 / E2 self-test PASS");
     }
     else
     {
         ESP_LOGE(TAG, "E3.3.7 Phase 4 / E2 self-test FAIL");
     }
     ESP_LOGI(TAG, "======================================");
 }




