/*
 * E3.1_unicast_diagnostic_main.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 * E3_1_unicast_diagnostic_main.c
  *
  * ENP v0.2
  *
  * TEMPORARY E3.1 ESP-NOW UNICAST DIAGNOSTIC
  *
  * Purpose:
  *
  *     Isolate ESP-NOW unicast communication from the
  *     broadcast/discovery problem.
  *
  * Test topology:
  *
  *     GATEWAY
  *        |
  *        | ESP-NOW UNICAST
  *        v
  *     SENSOR
  *        |
  *        | ESP-NOW UNICAST RESPONSE
  *        v
  *     GATEWAY
  *
  * This test deliberately does NOT use:
  *
  *     - ESP-NOW broadcast
  *     - ENP routing
  *     - route discovery
  *     - RREQ
  *     - RREP
  *     - RERR
  *
  * It tests only:
  *
  *     Wi-Fi
  *       ->
  *     ESP-NOW transport
  *       ->
  *     ENP transport abstraction
  *       ->
  *     unicast TX
  *       ->
  *     unicast RX
  *       ->
  *     receive callback
  *       ->
  *     unicast response
  *
  * Existing project role configuration is used:
  *
  *     CONFIG_DEVICE_ROLE_GATEWAY
  *     CONFIG_DEVICE_ROLE_SENSOR
  *
  * IMPORTANT:
  *
  * The MAC addresses below are the addresses observed during
  * the current E3.1 diagnostic run:
  *
  *     Gateway:
  *         94:E6:86:0D:11:8C
  *
  *     Sensor:
  *         78:21:84:E6:19:84
  *
  * These addresses are intentionally hard-coded ONLY for this
  * temporary diagnostic.
  *
  * Do not move these addresses into the ENP architecture.
  */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>
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

 #include "link/enp_transport_espnow.h"
 #include "link/enp_transport_wifi.h"


 /*----------------------------------------------------------
  * Logging
  *---------------------------------------------------------*/

 static const char *TAG =
         "E3_UCAST";


 /*----------------------------------------------------------
  * ENP Context
  *---------------------------------------------------------*/

 static enp_context_t s_context;


 /*----------------------------------------------------------
  * Runtime State
  *---------------------------------------------------------*/

 static volatile bool s_initialized = false;

 static volatile bool s_unicast_received = false;

 static volatile bool s_response_received = false;

 static volatile bool s_payload_error = false;


 /*----------------------------------------------------------
  * Last received frame
  *---------------------------------------------------------*/

 static enp_transport_address_t s_last_source;

 static uint8_t s_last_payload[64];

 static size_t s_last_payload_length = 0U;


 /*----------------------------------------------------------
  * Counters
  *---------------------------------------------------------*/

 static unsigned s_rx_count = 0U;

 static unsigned s_tx_count = 0U;


 /*----------------------------------------------------------
  * Test Configuration
  *---------------------------------------------------------*/

 #define E3_UCAST_TEST_TIMEOUT_MS       10000U


 #define E3_UCAST_PAYLOAD \
         "ENP-E3.1-DIRECT-UNICAST"

 #define E3_UCAST_RESPONSE_PAYLOAD \
         "ENP-E3.1-UNICAST-RESPONSE"


 /*----------------------------------------------------------
  * ENP Configuration
  *---------------------------------------------------------*/

 #define E3_NETWORK_ID \
         ((enp_network_id_t)1U)

 #define E3_GATEWAY_NODE_ID \
         ((enp_node_id_t)1U)

 #define E3_SENSOR_NODE_ID \
         ((enp_node_id_t)2U)


 /*----------------------------------------------------------
  * Temporary Diagnostic MAC Addresses
  *---------------------------------------------------------*/

 /*
  * Gateway:
  *
  *     94:E6:86:0D:11:8C
  */
 #define E3_GATEWAY_MAC \
     { \
         0x94U, \
         0xE6U, \
         0x86U, \
         0x0DU, \
         0x11U, \
         0x8CU  \
     }


 /*
  * Sensor:
  *
  *     78:21:84:E6:19:84
  */
 #define E3_SENSOR_MAC \
     { \
         0x78U, \
         0x21U, \
         0x84U, \
         0xE6U, \
         0x19U, \
         0x84U  \
     }


 /*----------------------------------------------------------
  * Test Helpers
  *---------------------------------------------------------*/

 #define PASS(name) \
     ESP_LOGI( \
         TAG, \
         "PASS: %s", \
         name)


 #define FAIL(name) \
     do \
     { \
         ESP_LOGE( \
             TAG, \
             "FAIL: %s", \
             name); \
         return false; \
     } \
     while (0)


 #define CHECK(condition, name) \
     do \
     { \
         if (condition) \
         { \
             PASS(name); \
         } \
         else \
         { \
             FAIL(name); \
         } \
     } \
     while (0)


 /*----------------------------------------------------------
  * NVS
  *---------------------------------------------------------*/

 static void nvs_init(void)
 {
     esp_err_t err =
             nvs_flash_init();

     if ((err == ESP_ERR_NVS_NO_FREE_PAGES) ||
         (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
     {
         ESP_ERROR_CHECK(
                 nvs_flash_erase());

         err =
                 nvs_flash_init();
     }

     ESP_ERROR_CHECK(err);
 }


 /*----------------------------------------------------------
  * Wi-Fi Wait
  *---------------------------------------------------------*/

 static bool wait_for_wifi(void)
 {
     const uint32_t start =
             xTaskGetTickCount() *
             portTICK_PERIOD_MS;

     while (!enp_wifi_is_connected())
     {
         vTaskDelay(
                 pdMS_TO_TICKS(100U));

         const uint32_t now =
                 xTaskGetTickCount() *
                 portTICK_PERIOD_MS;

         if ((now - start) >=
             E3_UCAST_TEST_TIMEOUT_MS)
         {
             return false;
         }
     }

     return true;
 }


 /*----------------------------------------------------------
  * ENP Configuration
  *---------------------------------------------------------*/

 static enp_config_t make_config(void)
 {
     enp_config_t config =
     {
         0
     };

     config.network_id =
             E3_NETWORK_ID;

 #if CONFIG_DEVICE_ROLE_GATEWAY

     config.node_id =
             E3_GATEWAY_NODE_ID;

     config.role =
             ENP_ROLE_GATEWAY;

 #else

     config.node_id =
             E3_SENSOR_NODE_ID;

     config.role =
             ENP_ROLE_SENSOR;

 #endif

     return config;
 }


 /*----------------------------------------------------------
  * Payload Comparison
  *---------------------------------------------------------*/

 static bool payload_equals(
         const void *data,
         size_t length,
         const char *expected)
 {
     if ((data == NULL) ||
         (expected == NULL))
     {
         return false;
     }

     const size_t expected_length =
             strlen(expected);

     if (length != expected_length)
     {
         return false;
     }

     return memcmp(
                data,
                expected,
                expected_length) == 0;
 }


 /*----------------------------------------------------------
  * MAC Address Helper
  *---------------------------------------------------------*/

 static enp_transport_address_t
 make_mac_address(
         const uint8_t mac[6])
 {
     enp_transport_address_t address =
     {
         0
     };

     address.length =
             6U;

     memcpy(
             address.value,
             mac,
             6U);

     return address;
 }


 /*----------------------------------------------------------
  * MAC Logging
  *---------------------------------------------------------*/

 static void log_address(
         const char *label,
         const enp_transport_address_t *address)
 {
     if (address == NULL)
     {
         return;
     }

     if (address->length != 6U)
     {
         ESP_LOGI(
             TAG,
             "%s: invalid length=%u",
             label,
             (unsigned)address->length);

         return;
     }

     ESP_LOGI(
         TAG,
         "%s: "
         "%02X:%02X:%02X:%02X:%02X:%02X",
         label,
         address->value[0],
         address->value[1],
         address->value[2],
         address->value[3],
         address->value[4],
         address->value[5]);
 }


 /*----------------------------------------------------------
  * ENP Receive Callback
  *---------------------------------------------------------*/

 static void e3_receive_callback(
         const enp_transport_address_t *source,
         const void *data,
         size_t length)
 {
     if ((source == NULL) ||
         (data == NULL) ||
         (length == 0U))
     {
         return;
     }

     ++s_rx_count;

     ESP_LOGI(
         TAG,
         "ENP RX callback: %u bytes",
         (unsigned)length);

     log_address(
         "ENP RX source",
         source);

     /*
      * Save source and payload for the test.
      */
     if (length >
         sizeof(s_last_payload))
     {
         s_payload_error =
                 true;

         ESP_LOGE(
             TAG,
             "Received payload too large");

         return;
     }

     s_last_source =
             *source;

     memcpy(
         s_last_payload,
         data,
         length);

     s_last_payload_length =
             length;


 #if CONFIG_DEVICE_ROLE_GATEWAY

     /*
      * Gateway expects the Sensor response.
      */
     if (payload_equals(
             data,
             length,
             E3_UCAST_RESPONSE_PAYLOAD))
     {
         s_response_received =
                 true;

         ESP_LOGI(
             TAG,
             "Gateway received Sensor "
             "unicast response");

         return;
     }

     s_payload_error =
             true;

     ESP_LOGE(
         TAG,
         "Gateway received unexpected "
         "payload");

 #else

     /*
      * Sensor expects the direct Gateway
      * unicast frame.
      */
     if (payload_equals(
             data,
             length,
             E3_UCAST_PAYLOAD))
     {
         s_unicast_received =
                 true;

         ESP_LOGI(
             TAG,
             "Sensor received Gateway "
             "direct unicast");

         /*
          * Reply using the source address
          * supplied by the ENP transport.
          *
          * This is an important part of
          * the diagnostic:
          *
          *     ESP-NOW RX MAC
          *          ->
          *     ENP transport address
          *          ->
          *     enp_transport_send()
          *          ->
          *     ESP-NOW unicast
          */
         const esp_err_t err =
                 enp_transport_send(
                     s_context.transport,
                     source,
                     E3_UCAST_RESPONSE_PAYLOAD,
                     strlen(
                         E3_UCAST_RESPONSE_PAYLOAD));

         if (err != ESP_OK)
         {
             s_payload_error =
                     true;

             ESP_LOGE(
                 TAG,
                 "Sensor response send failed: %s",
                 esp_err_to_name(err));

             return;
         }

         ++s_tx_count;

         ESP_LOGI(
             TAG,
             "Sensor response submitted");

         return;
     }

     s_payload_error =
             true;

     ESP_LOGE(
         TAG,
         "Sensor received unexpected "
         "payload");

 #endif
 }


 /*----------------------------------------------------------
  * Platform Initialization
  *---------------------------------------------------------*/

 static bool initialize_platform(void)
 {
     ESP_LOGI(
         TAG,
         "======================================");

 #if CONFIG_DEVICE_ROLE_GATEWAY

     ESP_LOGI(
         TAG,
         "E3.1 DIRECT UNICAST DIAGNOSTIC");
     ESP_LOGI(
         TAG,
         "Role: GATEWAY");

 #else

     ESP_LOGI(
         TAG,
         "E3.1 DIRECT UNICAST DIAGNOSTIC");
     ESP_LOGI(
         TAG,
         "Role: SENSOR");

 #endif

     ESP_LOGI(
         TAG,
         "======================================");

     nvs_init();

     CHECK(
         esp_netif_init() == ESP_OK,
         "ESP-NETIF initialized");

     CHECK(
         esp_event_loop_create_default() == ESP_OK,
         "default event loop initialized");

     CHECK(
         enp_wifi_init() == ESP_OK,
         "Wi-Fi initialized");

     ESP_LOGI(
         TAG,
         "Waiting for Wi-Fi connection...");

     CHECK(
         wait_for_wifi(),
         "Wi-Fi connected");

     ESP_LOGI(
         TAG,
         "Wi-Fi channel: %u",
         (unsigned)enp_wifi_get_channel());

     return true;
 }


 /*----------------------------------------------------------
  * ENP Initialization
  *---------------------------------------------------------*/

 static bool initialize_enp(void)
 {
     const enp_config_t config =
             make_config();

     ESP_LOGI(
         TAG,
         "ENP network: %u",
         (unsigned)config.network_id);

     ESP_LOGI(
         TAG,
         "ENP node: %u",
         (unsigned)config.node_id);

 #if CONFIG_DEVICE_ROLE_GATEWAY

     ESP_LOGI(
         TAG,
         "ENP role: GATEWAY");

 #else

     ESP_LOGI(
         TAG,
         "ENP role: SENSOR");

 #endif

     enp_transport_t *transport =
             enp_transport_espnow_get();

     CHECK(
         transport != NULL,
         "ESP-NOW transport instance obtained");

     CHECK(
         enp_context_init(
             &s_context,
             transport,
             &config) == ESP_OK,
         "ENP context initialized "
         "with ESP-NOW");

     CHECK(
         enp_transport_set_receive_callback(
             s_context.transport,
             e3_receive_callback) == ESP_OK,
         "ESP-NOW receive callback registered");

     s_initialized =
             true;

     return true;
 }


 /*----------------------------------------------------------
  * Gateway Test
  *---------------------------------------------------------*/

 #if CONFIG_DEVICE_ROLE_GATEWAY

 static bool run_gateway_test(void)
 {
     const uint8_t sensor_mac[6] =
             E3_SENSOR_MAC;

     const enp_transport_address_t sensor =
             make_mac_address(
                 sensor_mac);

     ESP_LOGI(
         TAG,
         "--------------------------------------");

     ESP_LOGI(
         TAG,
         "Gateway -> Sensor DIRECT UNICAST");

     log_address(
         "Destination",
         &sensor);

     ESP_LOGI(
         TAG,
         "Payload: %s",
         E3_UCAST_PAYLOAD);

     /*
      * Clear previous state.
      */
     s_response_received =
             false;

     s_payload_error =
             false;

     s_last_payload_length =
             0U;

     /*
      * Send directly to the Sensor MAC.
      */
     const esp_err_t err =
             enp_transport_send(
                 s_context.transport,
                 &sensor,
                 E3_UCAST_PAYLOAD,
                 strlen(E3_UCAST_PAYLOAD));

     CHECK(
         err == ESP_OK,
         "Gateway unicast send accepted");

     ++s_tx_count;

     PASS(
         "Gateway unicast frame submitted "
         "to ESP-NOW");

     /*
      * The generic transport send returning
      * ESP_OK means that the frame was accepted
      * by the ESP-NOW API.
      *
      * The actual wireless delivery result is
      * reported asynchronously by the ESP-NOW
      * send callback.
      *
      * We therefore wait for the Sensor's
      * response as the end-to-end assertion.
      */
     ESP_LOGI(
         TAG,
         "Waiting for Sensor response...");

     const uint32_t start =
             xTaskGetTickCount() *
             portTICK_PERIOD_MS;

     while (!s_response_received)
     {
         vTaskDelay(
                 pdMS_TO_TICKS(50U));

         const uint32_t now =
                 xTaskGetTickCount() *
                 portTICK_PERIOD_MS;

         if ((now - start) >=
             E3_UCAST_TEST_TIMEOUT_MS)
         {
             break;
         }
     }

     CHECK(
         s_response_received,
         "Sensor unicast response received");

     CHECK(
         !s_payload_error,
         "Sensor response payload valid");

     CHECK(
         s_last_source.length == 6U,
         "Sensor response source has "
         "ESP-NOW MAC length");

     CHECK(
         payload_equals(
             s_last_payload,
             s_last_payload_length,
             E3_UCAST_RESPONSE_PAYLOAD),
         "Sensor response payload integrity");

     log_address(
         "Sensor response source",
         &s_last_source);

     ESP_LOGI(
         TAG,
         "Gateway TX frames submitted: %u",
         s_tx_count);

     ESP_LOGI(
         TAG,
         "Gateway RX frames received: %u",
         s_rx_count);

     return true;
 }


 /*----------------------------------------------------------
  * Sensor Test
  *---------------------------------------------------------*/

 #else

 static bool run_sensor_test(void)
 {
     const uint8_t gateway_mac[6] =
             E3_GATEWAY_MAC;

     const enp_transport_address_t gateway =
             make_mac_address(
                 gateway_mac);

     ESP_LOGI(
         TAG,
         "--------------------------------------");

     ESP_LOGI(
         TAG,
         "Sensor waiting for Gateway "
         "DIRECT UNICAST");

     log_address(
         "Expected Gateway",
         &gateway);

     ESP_LOGI(
         TAG,
         "Expected payload: %s",
         E3_UCAST_PAYLOAD);

     /*
      * The Sensor does not transmit anything
      * initially.
      *
      * It waits for the Gateway to send the
      * direct unicast frame.
      */
     const uint32_t start =
             xTaskGetTickCount() *
             portTICK_PERIOD_MS;

     while (!s_unicast_received)
     {
         vTaskDelay(
                 pdMS_TO_TICKS(50U));

         const uint32_t now =
                 xTaskGetTickCount() *
                 portTICK_PERIOD_MS;

         if ((now - start) >=
             E3_UCAST_TEST_TIMEOUT_MS)
         {
             break;
         }
     }

     CHECK(
         s_unicast_received,
         "Gateway unicast received");

     CHECK(
         !s_payload_error,
         "Gateway unicast payload valid");

     CHECK(
         s_last_source.length == 6U,
         "Gateway source has "
         "ESP-NOW MAC length");

     CHECK(
         payload_equals(
             s_last_payload,
             s_last_payload_length,
             E3_UCAST_PAYLOAD),
         "Gateway unicast payload integrity");

     log_address(
         "Actual Gateway source",
         &s_last_source);

     /*
      * The receive callback has already submitted
      * the response using the source address.
      *
      * At this point the response send has been
      * accepted by the transport.
      */
     PASS(
         "Sensor response submitted "
         "to Gateway");

     ESP_LOGI(
         TAG,
         "Sensor TX frames submitted: %u",
         s_tx_count);

     ESP_LOGI(
         TAG,
         "Sensor RX frames received: %u",
         s_rx_count);

     return true;
 }

 #endif


 /*----------------------------------------------------------
  * ENP Deinitialization
  *---------------------------------------------------------*/

 static bool deinitialize_enp(void)
 {
     CHECK(
         s_initialized,
         "ENP test runtime was initialized");

     const esp_err_t err =
             enp_context_deinit(
                 &s_context);

     CHECK(
         err == ESP_OK,
         "ENP context / ESP-NOW transport "
         "deinitialized");

     s_initialized =
             false;

     return true;
 }


 /*----------------------------------------------------------
  * Application Entry
  *---------------------------------------------------------*/

 void app_main(void)
 {
     bool ok =
             true;

     ESP_LOGI(
         TAG,
         "======================================");

     ESP_LOGI(
         TAG,
         "ENP v0.2 E3.1 "
         "ESP-NOW UNICAST DIAGNOSTIC");

     ESP_LOGI(
         TAG,
         "======================================");


     /*
      * Platform.
      */
     if (!initialize_platform())
     {
         ok =
                 false;
     }


     /*
      * ENP + ESP-NOW.
      */
     if (ok &&
         !initialize_enp())
     {
         ok =
                 false;
     }


     /*
      * Role-specific test.
      */
     if (ok)
     {

 #if CONFIG_DEVICE_ROLE_GATEWAY

         ok =
                 run_gateway_test();

 #else

         ok =
                 run_sensor_test();

 #endif

     }


     /*
      * Cleanup.
      */
     if (s_initialized)
     {
         if (!deinitialize_enp())
         {
             ok =
                     false;
         }
     }


     ESP_LOGI(
         TAG,
         "======================================");

 #if CONFIG_DEVICE_ROLE_GATEWAY

     if (ok)
     {
         ESP_LOGI(
             TAG,
             "ALL E3.1 GATEWAY "
             "UNICAST DIAGNOSTIC TESTS PASSED");
     }
     else
     {
         ESP_LOGE(
             TAG,
             "E3.1 GATEWAY "
             "UNICAST DIAGNOSTIC TEST FAILED");
     }

 #else

     if (ok)
     {
         ESP_LOGI(
             TAG,
             "ALL E3.1 SENSOR "
             "UNICAST DIAGNOSTIC TESTS PASSED");
     }
     else
     {
         ESP_LOGE(
             TAG,
             "E3.1 SENSOR "
             "UNICAST DIAGNOSTIC TEST FAILED");
     }

 #endif

     ESP_LOGI(
         TAG,
         "======================================");
 }




