/*
 * E3.3.7_p4_E4A_test_enp_dispatcher_local_dispatch_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7 Phase 4 / P4-E4A
 * Dispatcher local-dispatch boundary self-test.
 */

 #include <stdbool.h>
 #include <stdint.h>
 #include <string.h>

 #include "esp_log.h"

 #include "core/enp_context.h"
 #include "core/enp_duplicate.h"
 #include "core/dispatcher/enp_dispatcher.h"
 #include "core/protocol/enp_packet.h"
 #include "core/service/enp_service.h"

 static const char *TAG = "E3_3_7_P4_E4A";

 #define TEST_NETWORK_ID 1U
 #define TEST_LOCAL_NODE 2U
 #define TEST_REMOTE_NODE 1U
 #define TEST_SEQUENCE 0xE4A1U

 static uint32_t s_service_calls;
 static enp_transport_address_t s_last_source;
 static enp_sequence_t s_last_sequence;
static enp_duplicate_cache_t s_test_duplicate_cache;

 static esp_err_t test_service_process(
         enp_context_t *context,
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     (void)context;

     if ((packet == NULL) || (source == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     const enp_header_t *header = enp_packet_header_const(packet);
     if (header == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     ++s_service_calls;
     s_last_source = *source;
     s_last_sequence = header->sequence;

     return ESP_OK;
 }

 static const enp_service_t s_application_service = {
     .name = "test_application",
     .packet_type = ENP_PACKET_APPLICATION,
     .init = NULL,
     .process = test_service_process,
 };

 static bool make_packet(
         enp_packet_t *packet,
         enp_address_t source,
         enp_address_t destination,
         enp_sequence_t sequence)
 {
     if (packet == NULL)
     {
         return false;
     }

     enp_packet_init(
             packet,
             ENP_PACKET_APPLICATION,
             &source);

     enp_header_t *header = enp_packet_header(packet);
     if (header == NULL)
     {
         return false;
     }

     header->destination = destination;
     header->ttl = 8U;
     header->sequence = sequence;

     const uint8_t payload[] = {0x45U, 0x34U, 0x41U};
     memcpy(
             enp_packet_payload(packet),
             payload,
             sizeof(payload));

     return enp_packet_seal(
                packet,
                sizeof(payload)) == ESP_OK;
 }

 void app_main(void)
 {
     ESP_LOGI(TAG, "======================================");
     ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E4A LOCAL DISPATCH");
     ESP_LOGI(TAG, "Dispatcher local-dispatch boundary self-test");
     ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG, "======================================");

     bool pass = true;

     static enp_context_t context = {0};
     context.network.id = TEST_NETWORK_ID;
     context.network.local.id = TEST_LOCAL_NODE;

     if (enp_dispatcher_init(&context) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: dispatcher initialized");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: dispatcher initialized");
     }

     if (enp_dispatcher_register(&s_application_service) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: application service registered");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: application service registered");
     }

     static enp_packet_t packet;
     const enp_address_t source_address = {
         .network = TEST_NETWORK_ID,
         .node = TEST_REMOTE_NODE,
     };
     const enp_address_t destination_address = {
         .network = TEST_NETWORK_ID,
         .node = TEST_LOCAL_NODE,
     };

     static enp_transport_address_t source_transport = {0};
     source_transport.length = 6U;
     source_transport.value[0] = 0x10U;
     source_transport.value[1] = 0x20U;
     source_transport.value[2] = 0x30U;
     source_transport.value[3] = 0x40U;
     source_transport.value[4] = 0x50U;
     source_transport.value[5] = 0x60U;

     if (!make_packet(
                 &packet,
                 source_address,
                 destination_address,
                 TEST_SEQUENCE))
     {
         ESP_LOGE(TAG, "FAIL: local DATA packet constructed");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: local DATA packet constructed");
     }

     /*
      * Diagnostic isolation: verify the duplicate-cache module itself
      * before testing the dispatcher-owned cache. This does not change
      * production behavior and helps distinguish a cache-module fault
      * from dispatcher integration.
      */
     if (enp_duplicate_cache_init(&s_test_duplicate_cache) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: diagnostic duplicate cache initialized");
         pass = false;
     }
     else
     {
         bool duplicate = false;
         const uint32_t now_ms = enp_context_time_ms(&context);

         if (enp_duplicate_check_and_record(
                     &s_test_duplicate_cache,
                     &source_address,
                     TEST_SEQUENCE,
                     now_ms,
                     &duplicate) != ESP_OK || duplicate)
         {
             ESP_LOGE(TAG,
                      "FAIL: diagnostic duplicate cache first check unexpected");
             pass = false;
         }
         else if (enp_duplicate_check_and_record(
                     &s_test_duplicate_cache,
                     &source_address,
                     TEST_SEQUENCE,
                     enp_context_time_ms(&context),
                     &duplicate) != ESP_OK || !duplicate)
         {
             ESP_LOGE(TAG,
                      "FAIL: diagnostic duplicate cache did not suppress second identity");
             pass = false;
         }
         else
         {
             ESP_LOGI(TAG,
                      "PASS: duplicate-cache module suppresses identical source+sequence");
         }

         (void)enp_duplicate_cache_clear(&s_test_duplicate_cache);
     }

     s_service_calls = 0U;
     memset(&s_last_source, 0, sizeof(s_last_source));
     s_last_sequence = 0U;

     if (enp_dispatcher_dispatch_local(
                 &packet,
                 &source_transport) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: local dispatch returned error");
         pass = false;
     }
     else if (s_service_calls != 1U)
     {
         ESP_LOGE(TAG, "FAIL: local service was not invoked exactly once");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: local packet dispatched to registered service");
     }

     if ((s_last_sequence != TEST_SEQUENCE) ||
         (s_last_source.length != source_transport.length) ||
         (memcmp(
                 s_last_source.value,
                 source_transport.value,
                 source_transport.length) != 0))
     {
         ESP_LOGE(TAG, "FAIL: local dispatch did not preserve packet/source identity");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: local dispatch preserved packet/source identity");
     }

     /*
      * The same DATA identity must reach the local service again. This is
      * the key E4A property: local dispatch does not apply the dispatcher's
      * generic duplicate cache a second time. The data plane has already
      * made the DATA/ACK duplicate decision before this boundary.
      */
     if (enp_dispatcher_dispatch_local(
                 &packet,
                 &source_transport) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: repeated local dispatch returned error");
         pass = false;
     }
     else if (s_service_calls != 2U)
     {
         ESP_LOGE(TAG,
                  "FAIL: local dispatch applied generic duplicate suppression");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG,
                  "PASS: local dispatch bypassed generic dispatcher duplicate cache");
     }

     ESP_LOGI(TAG,
              "DIAG: normal dispatch #1 source=network=%u node=%lu seq=0x%08lX",
              (unsigned)source_address.network,
              (unsigned long)source_address.node,
              (unsigned long)TEST_SEQUENCE);

     if (enp_dispatcher_dispatch(
                 &packet,
                 &source_transport) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: normal dispatcher dispatch returned error");
         pass = false;
     }
     else if (s_service_calls != 3U)
     {
         ESP_LOGE(TAG, "FAIL: normal dispatcher first dispatch did not reach service");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: normal dispatcher dispatch reached service once");
     }

     ESP_LOGI(TAG,
              "DIAG: normal dispatch #2 source=network=%u node=%lu seq=0x%08lX service_calls_before=%lu",
              (unsigned)source_address.network,
              (unsigned long)source_address.node,
              (unsigned long)TEST_SEQUENCE,
              (unsigned long)s_service_calls);

     if (enp_dispatcher_dispatch(
                 &packet,
                 &source_transport) != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: normal dispatcher duplicate dispatch returned error");
         pass = false;
     }
     else if (s_service_calls == 3U)
     {
         ESP_LOGI(TAG,
                  "PASS: normal dispatcher generic duplicate cache suppressed duplicate");
     }
     else
     {
         ESP_LOGE(TAG,
                  "FAIL: normal dispatcher duplicate reached service again");
         pass = false;
     }

     if (enp_dispatcher_deinit() != ESP_OK)
     {
         ESP_LOGE(TAG, "FAIL: dispatcher deinitialized");
         pass = false;
     }
     else
     {
         ESP_LOGI(TAG, "PASS: dispatcher deinitialized");
     }

     if (pass)
     {
         ESP_LOGI(TAG, "--------------------------------------");
         ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E4A self-test PASS");
         ESP_LOGI(TAG, "======================================");
     }
     else
     {
         ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E4A self-test FAIL");
     }
 }
