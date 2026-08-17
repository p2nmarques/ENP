/*
 * E3.1_test_enp_espnow_gateway_sensor_main.c
 *
 *  Created on: Aug 14, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.1 real ESP-NOW transport test.
 *
 * Uses the project's existing:
 *
 *     CONFIG_DEVICE_ROLE_GATEWAY
 *     CONFIG_DEVICE_ROLE_SENSOR
 *
 * No E3-specific role definitions are introduced.
 *
 * Test topology:
 *
 *     GATEWAY  <====== ESP-NOW ======>  SENSOR
 *
 * The test validates:
 *     - platform/Wi-Fi initialization
 *     - ENP context + ESP-NOW transport initialization
 *     - receive callback registration
 *     - broadcast Gateway -> Sensor
 *     - Sensor -> Gateway response using the learned MAC
 *     - Gateway -> Sensor unicast
 *     - payload integrity
 *     - transport/context deinitialization
 *
 * The routing layer is deliberately not involved yet.
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

static const char *TAG = "E3_1";

static enp_context_t s_context;

static volatile bool s_initialized = false;

static volatile bool s_broadcast_received = false;
static volatile bool s_response_received = false;
static volatile bool s_unicast_received = false;

static volatile bool s_payload_error = false;

static enp_transport_address_t s_last_source;

static uint8_t s_last_payload[64];
static size_t s_last_payload_length = 0U;

static unsigned s_rx_count = 0U;
static unsigned s_tx_count = 0U;

#define E3_TEST_TIMEOUT_MS 10000U

#define E3_BROADCAST_PAYLOAD "ENP-E3.1-GATEWAY-BROADCAST"
#define E3_RESPONSE_PAYLOAD "ENP-E3.1-SENSOR-RESPONSE"
#define E3_UNICAST_PAYLOAD "ENP-E3.1-GATEWAY-UNICAST"

#define E3_GATEWAY_NETWORK_ID ((enp_network_id_t)1U)
#define E3_GATEWAY_NODE_ID ((enp_node_id_t)1U)
#define E3_SENSOR_NODE_ID ((enp_node_id_t)2U)

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)

#define FAIL(name)                                                             \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", name);                                       \
		return false;                                                          \
	} while (0)

#define CHECK(condition, name)                                                 \
	do {                                                                       \
		if (condition) {                                                       \
			PASS(name);                                                        \
		} else {                                                               \
			FAIL(name);                                                        \
		}                                                                      \
	} while (0)

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
	const uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!enp_wifi_is_connected()) {
		vTaskDelay(pdMS_TO_TICKS(100U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - start) >= E3_TEST_TIMEOUT_MS) {
			return false;
		}
	}

	return true;
}

static enp_config_t make_config(void) {
	enp_config_t config = {0};

	config.network_id = E3_GATEWAY_NETWORK_ID;

#if CONFIG_DEVICE_ROLE_GATEWAY

	config.node_id = E3_GATEWAY_NODE_ID;
	config.role = ENP_ROLE_GATEWAY;

#else

	config.node_id = E3_SENSOR_NODE_ID;
	config.role = ENP_ROLE_SENSOR;

#endif

	return config;
}

static bool payload_equals(const void *data, size_t length,
						   const char *expected) {
	const size_t expected_length = strlen(expected);

	return data != NULL && length == expected_length &&
		   memcmp(data, expected, expected_length) == 0;
}

static void e3_receive_callback(const enp_transport_address_t *source,
								const void *data, size_t length) {
	if ((source == NULL) || (data == NULL) || (length == 0U)) {
		return;
	}

	++s_rx_count;

	if (length > sizeof(s_last_payload)) {
		s_payload_error = true;
		return;
	}

	s_last_source = *source;

	memcpy(s_last_payload, data, length);

	s_last_payload_length = length;

#if CONFIG_DEVICE_ROLE_SENSOR

	/*
	 * The first frame is the Gateway broadcast.
	 *
	 * Reply using the source MAC supplied by the ESP-NOW
	 * transport. This exercises:
	 *
	 *     receive callback
	 *         ->
	 *     transport address
	 *         ->
	 *     unicast send
	 */
	if (payload_equals(data, length, E3_BROADCAST_PAYLOAD)) {

		s_broadcast_received = true;

		ESP_LOGI(TAG, "Sensor received Gateway broadcast");

		const esp_err_t err =
			enp_transport_send(s_context.transport, source, E3_RESPONSE_PAYLOAD,
							   strlen(E3_RESPONSE_PAYLOAD));

		if (err != ESP_OK) {
			s_payload_error = true;

			ESP_LOGE(TAG, "Sensor response send failed: %s",
					 esp_err_to_name(err));

			return;
		}

		++s_tx_count;

		ESP_LOGI(TAG, "Sensor response sent to Gateway");
	}

	/*
	 * The second frame is Gateway's unicast frame.
	 */
	else if (payload_equals(data, length, E3_UNICAST_PAYLOAD)) {

		s_unicast_received = true;

		ESP_LOGI(TAG, "Sensor received Gateway unicast");
	}

	else {
		s_payload_error = true;

		ESP_LOGE(TAG, "Sensor received unexpected payload");
	}

#else

	/*
	 * Gateway expects the Sensor response.
	 */
	if (payload_equals(data, length, E3_RESPONSE_PAYLOAD)) {

		s_response_received = true;

		ESP_LOGI(TAG, "Gateway received Sensor response");
	}

	else {
		s_payload_error = true;

		ESP_LOGE(TAG, "Gateway received unexpected payload");
	}

#endif
}

static bool initialize_platform(void) {
	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY

	ESP_LOGI(TAG, "E3.1 Gateway ESP-NOW transport test");

#else

	ESP_LOGI(TAG, "E3.1 Sensor ESP-NOW transport test");

#endif

	ESP_LOGI(TAG, "======================================");

	nvs_init();

	CHECK(esp_netif_init() == ESP_OK, "ESP-NETIF initialized");

	CHECK(esp_event_loop_create_default() == ESP_OK,
		  "default event loop initialized");

	CHECK(enp_wifi_init() == ESP_OK, "Wi-Fi initialized");

	ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

	CHECK(wait_for_wifi(), "Wi-Fi connected");

	ESP_LOGI(TAG, "Wi-Fi channel: %u", (unsigned)enp_wifi_get_channel());

	return true;
}

static bool initialize_enp(void) {
	const enp_config_t config = make_config();

	ESP_LOGI(TAG, "ENP network: %u", (unsigned)config.network_id);

	ESP_LOGI(TAG, "ENP node: %u", (unsigned)config.node_id);

#if CONFIG_DEVICE_ROLE_GATEWAY

	ESP_LOGI(TAG, "ENP role: GATEWAY");

#else

	ESP_LOGI(TAG, "ENP role: SENSOR");

#endif

	enp_transport_t *transport = enp_transport_espnow_get();

	CHECK(transport != NULL, "ESP-NOW transport instance obtained");

	CHECK(enp_context_init(&s_context, transport, &config) == ESP_OK,
		  "ENP context initialized with ESP-NOW");

	CHECK(enp_transport_set_receive_callback(s_context.transport,
											 e3_receive_callback) == ESP_OK,
		  "ESP-NOW receive callback registered");

	s_initialized = true;

	return true;
}

#if CONFIG_DEVICE_ROLE_GATEWAY

static bool run_gateway_test(void) {
	const enp_transport_address_t broadcast = {.length = 0U};

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Gateway -> Sensor broadcast test");

	CHECK(enp_transport_send(s_context.transport, &broadcast,
							 E3_BROADCAST_PAYLOAD,
							 strlen(E3_BROADCAST_PAYLOAD)) == ESP_OK,
		  "Gateway broadcast send accepted");

	++s_tx_count;

	ESP_LOGI(TAG, "Waiting for Sensor response...");

	const uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!s_response_received) {
		vTaskDelay(pdMS_TO_TICKS(50U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - start) >= E3_TEST_TIMEOUT_MS) {
			break;
		}
	}

	CHECK(s_response_received, "Sensor response received");

	CHECK(!s_payload_error, "Gateway received valid response payload");

	CHECK(s_last_source.length == 6U,
		  "Sensor source address has ESP-NOW MAC length");

	CHECK(payload_equals(s_last_payload, s_last_payload_length,
						 E3_RESPONSE_PAYLOAD),
		  "Sensor response payload integrity");

	/*
	 * The response source is the Sensor's MAC address.
	 * Reuse it as the unicast destination.
	 */
	const enp_transport_address_t sensor = s_last_source;

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Gateway -> Sensor unicast test");

	s_unicast_received = false;
	s_payload_error = false;

	CHECK(enp_transport_send(s_context.transport, &sensor, E3_UNICAST_PAYLOAD,
							 strlen(E3_UNICAST_PAYLOAD)) == ESP_OK,
		  "Gateway unicast send accepted");

	++s_tx_count;

	/*
	 * The Sensor receives this frame asynchronously.
	 * Gateway cannot observe the Sensor's local receive flag,
	 * so the unicast send acceptance is the transport-side
	 * assertion. The Sensor log provides the receive assertion.
	 */
	PASS("Gateway unicast frame submitted to ESP-NOW");

	ESP_LOGI(TAG, "Gateway TX frames submitted: %u", s_tx_count);

	ESP_LOGI(TAG, "Gateway RX frames received: %u", s_rx_count);

	return true;
}

#else

static bool run_sensor_test(void) {
	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Sensor waiting for Gateway broadcast");

	const uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!s_broadcast_received) {
		vTaskDelay(pdMS_TO_TICKS(50U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - start) >= E3_TEST_TIMEOUT_MS) {
			break;
		}
	}

	CHECK(s_broadcast_received, "Gateway broadcast received");

	CHECK(!s_payload_error, "Gateway broadcast payload is valid");

	CHECK(s_last_source.length == 6U,
		  "Gateway source address has ESP-NOW MAC length");

	CHECK(payload_equals(s_last_payload, s_last_payload_length,
						 E3_BROADCAST_PAYLOAD),
		  "Gateway broadcast payload integrity");

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Sensor waiting for Gateway unicast");

	const uint32_t unicast_start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!s_unicast_received) {
		vTaskDelay(pdMS_TO_TICKS(50U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - unicast_start) >= E3_TEST_TIMEOUT_MS) {
			break;
		}
	}

	CHECK(s_unicast_received, "Gateway unicast received");

	CHECK(!s_payload_error, "Gateway unicast payload is valid");

	CHECK(payload_equals(s_last_payload, s_last_payload_length,
						 E3_UNICAST_PAYLOAD),
		  "Gateway unicast payload integrity");

	ESP_LOGI(TAG, "Sensor TX frames submitted: %u", s_tx_count);

	ESP_LOGI(TAG, "Sensor RX frames received: %u", s_rx_count);

	return true;
}

#endif

static bool deinitialize_enp(void) {
	CHECK(s_initialized, "ENP test runtime was initialized");

	const esp_err_t err = enp_context_deinit(&s_context);

	CHECK(err == ESP_OK, "ENP context / ESP-NOW transport deinitialized");

	s_initialized = false;

	return true;
}

void app_main(void) {
	bool ok = true;

	if (!initialize_platform()) {
		ok = false;
	}

	if (ok && !initialize_enp()) {
		ok = false;
	}

	if (ok) {

#if CONFIG_DEVICE_ROLE_GATEWAY

		ok = run_gateway_test();

#else

		ok = run_sensor_test();

#endif
	}

	if (s_initialized) {
		if (!deinitialize_enp()) {
			ok = false;
		}
	}

	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY

	if (ok) {
		ESP_LOGI(TAG, "ALL E3.1 GATEWAY ESP-NOW TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "E3.1 GATEWAY ESP-NOW TEST FAILED");
	}

#else

	if (ok) {
		ESP_LOGI(TAG, "ALL E3.1 SENSOR ESP-NOW TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "E3.1 SENSOR ESP-NOW TEST FAILED");
	}

#endif

	ESP_LOGI(TAG, "======================================");
}
