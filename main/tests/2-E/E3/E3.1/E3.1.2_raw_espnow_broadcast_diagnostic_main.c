/*
 * E3_1_raw_espnow_broadcast_diagnostic_main.c
 *
 * TEMPORARY DIAGNOSTIC
 *
 * Purpose:
 *   Determine whether raw ESP-NOW broadcast works independently
 *   of the ENP transport abstraction.
 *
 * Topology:
 *
 *   GATEWAY  -- raw esp_now_send(FF:FF:FF:FF:FF:FF) --> SENSOR
 *
 * No ENP context, routing, RREQ, RREP or RERR is used.
 *
 * Existing project roles are used:
 *   CONFIG_DEVICE_ROLE_GATEWAY
 *   CONFIG_DEVICE_ROLE_SENSOR
 *
 * Gateway MAC:
 *   94:E6:86:0D:11:8C
 *
 * Sensor MAC:
 *   78:21:84:E6:19:84
 *
 * IMPORTANT:
 *   This is diagnostic code only. Do not merge it into the
 *   final ENP transport architecture.
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
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "link/enp_transport_wifi.h"

static const char *TAG = "E3_RAW";

#define TEST_TIMEOUT_MS 10000U

#define BROADCAST_PAYLOAD "ENP-E3.1-RAW-BROADCAST"

#define RESPONSE_PAYLOAD "ENP-E3.1-RAW-RESPONSE"

static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
														  0xFF, 0xFF, 0xFF};

static volatile bool s_received = false;
static volatile bool s_response_received = false;
static volatile bool s_payload_error = false;

static uint8_t s_last_source[ESP_NOW_ETH_ALEN];
static uint8_t s_last_payload[64];
static size_t s_last_length = 0U;

static unsigned s_tx_count = 0U;
static unsigned s_rx_count = 0U;

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

		if ((now - start) >= TEST_TIMEOUT_MS) {
			return false;
		}
	}

	return true;
}

static void log_mac(const char *label, const uint8_t *mac) {
	ESP_LOGI(TAG, "%s: %02X:%02X:%02X:%02X:%02X:%02X", label, mac[0], mac[1],
			 mac[2], mac[3], mac[4], mac[5]);
}

static bool payload_equals(const void *data, size_t length,
						   const char *expected) {
	const size_t expected_length = strlen(expected);

	return data != NULL && length == expected_length &&
		   memcmp(data, expected, expected_length) == 0;
}

static void raw_rx_callback(const esp_now_recv_info_t *info,
							const uint8_t *data, int len) {
	if ((info == NULL) || (data == NULL) || (len <= 0)) {
		return;
	}

	++s_rx_count;

	log_mac("RAW RX source", info->src_addr);

	ESP_LOGI(TAG, "RAW ESP-NOW RX: %d bytes", len);

	if ((size_t)len > sizeof(s_last_payload)) {
		s_payload_error = true;
		return;
	}

	memcpy(s_last_source, info->src_addr, ESP_NOW_ETH_ALEN);

	memcpy(s_last_payload, data, (size_t)len);

	s_last_length = (size_t)len;

#if CONFIG_DEVICE_ROLE_SENSOR

	if (payload_equals(data, (size_t)len, BROADCAST_PAYLOAD)) {

		s_received = true;

		ESP_LOGI(TAG, "Sensor received raw broadcast");

		/*
		 * ESP-NOW requires a peer entry for unicast TX.
		 *
		 * For this diagnostic we deliberately attempt the peer
		 * addition every time. This removes any ambiguity around
		 * esp_now_is_peer_exist() and gives us an explicit result.
		 */
		esp_now_peer_info_t response_peer = {0};

		memcpy(response_peer.peer_addr, info->src_addr, ESP_NOW_ETH_ALEN);

		response_peer.channel = 0;
		response_peer.ifidx = WIFI_IF_STA;
		response_peer.encrypt = false;

		ESP_LOGI(TAG,
				 "Adding response peer: "
				 "%02X:%02X:%02X:%02X:%02X:%02X",
				 response_peer.peer_addr[0], response_peer.peer_addr[1],
				 response_peer.peer_addr[2], response_peer.peer_addr[3],
				 response_peer.peer_addr[4], response_peer.peer_addr[5]);

		esp_err_t peer_err = esp_now_add_peer(&response_peer);

		if (peer_err == ESP_OK) {
			ESP_LOGI(TAG, "Gateway peer add: SUCCESS");
		} else if (peer_err == ESP_ERR_ESPNOW_EXIST) {
			ESP_LOGI(TAG, "Gateway peer add: ALREADY EXISTS");
		} else {
			s_payload_error = true;

			ESP_LOGE(TAG, "Gateway peer add: FAILED: %s",
					 esp_err_to_name(peer_err));

			return;
		}

		ESP_LOGI(TAG, "Sending raw unicast response");

		esp_err_t err =
			esp_now_send(info->src_addr, (const uint8_t *)RESPONSE_PAYLOAD,
						 strlen(RESPONSE_PAYLOAD));

		if (err != ESP_OK) {
			s_payload_error = true;

			ESP_LOGE(TAG, "Raw response send failed: %s", esp_err_to_name(err));

			return;
		}

		++s_tx_count;

		ESP_LOGI(TAG, "Sensor raw unicast response submitted");
	} else {
		s_payload_error = true;

		ESP_LOGE(TAG, "Sensor received unexpected payload");
	}

#else

	if (payload_equals(data, (size_t)len, RESPONSE_PAYLOAD)) {

		s_response_received = true;

		ESP_LOGI(TAG, "Gateway received raw response");
	} else {
		s_payload_error = true;

		ESP_LOGE(TAG, "Gateway received unexpected payload");
	}

#endif
}

static bool initialize_platform(void) {
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "SOURCE BUILD: PEER-FIX-V3");

#if CONFIG_DEVICE_ROLE_GATEWAY
	ESP_LOGI(TAG, "E3.1 RAW ESP-NOW BROADCAST");
	ESP_LOGI(TAG, "ROLE: GATEWAY");
#else
	ESP_LOGI(TAG, "E3.1 RAW ESP-NOW BROADCAST");
	ESP_LOGI(TAG, "ROLE: SENSOR");
#endif

	ESP_LOGI(TAG, "======================================");

	nvs_init();

	if (esp_netif_init() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ESP-NETIF initialization");
		return false;
	}

	ESP_LOGI(TAG, "PASS: ESP-NETIF initialized");

	if (esp_event_loop_create_default() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: default event loop initialization");
		return false;
	}

	ESP_LOGI(TAG, "PASS: default event loop initialized");

	if (enp_wifi_init() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: Wi-Fi initialization");
		return false;
	}

	ESP_LOGI(TAG, "PASS: Wi-Fi initialized");

	ESP_LOGI(TAG, "Waiting for Wi-Fi connection...");

	if (!wait_for_wifi()) {
		ESP_LOGE(TAG, "FAIL: Wi-Fi connection");
		return false;
	}

	ESP_LOGI(TAG, "PASS: Wi-Fi connected");

	ESP_LOGI(TAG, "Wi-Fi channel: %u", (unsigned)enp_wifi_get_channel());

	return true;
}

static bool initialize_raw_espnow(void) {
	esp_err_t err;

	err = esp_now_init();

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: esp_now_init(): %s", esp_err_to_name(err));
		return false;
	}

	ESP_LOGI(TAG, "PASS: esp_now_init()");

	err = esp_now_register_recv_cb(raw_rx_callback);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: esp_now_register_recv_cb(): %s",
				 esp_err_to_name(err));

		esp_now_deinit();

		return false;
	}

	ESP_LOGI(TAG, "PASS: raw RX callback registered");

	/*
	 * Add the broadcast peer explicitly.
	 *
	 * This duplicates only the peer setup for this diagnostic;
	 * the ENP transport is deliberately not used.
	 */
	esp_now_peer_info_t peer = {0};

	memcpy(peer.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);

	peer.channel = 0;
	peer.ifidx = WIFI_IF_STA;
	peer.encrypt = false;

	if (esp_now_is_peer_exist(s_broadcast_mac)) {

		ESP_LOGI(TAG, "Broadcast peer already exists");
	} else {
		err = esp_now_add_peer(&peer);

		if (err != ESP_OK) {
			ESP_LOGE(TAG, "FAIL: broadcast peer add: %s", esp_err_to_name(err));

			esp_now_deinit();

			return false;
		}

		ESP_LOGI(TAG, "PASS: raw broadcast peer added");
	}

	return true;
}

#if CONFIG_DEVICE_ROLE_GATEWAY

static bool run_gateway_test(void) {
	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Gateway RAW broadcast test");

	log_mac("Destination", s_broadcast_mac);

	ESP_LOGI(TAG, "Payload: %s", BROADCAST_PAYLOAD);

	esp_err_t err =
		esp_now_send(s_broadcast_mac, (const uint8_t *)BROADCAST_PAYLOAD,
					 strlen(BROADCAST_PAYLOAD));

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: esp_now_send(): %s", esp_err_to_name(err));

		return false;
	}

	++s_tx_count;

	ESP_LOGI(TAG, "PASS: raw broadcast submitted");

	const uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!s_response_received) {
		vTaskDelay(pdMS_TO_TICKS(50U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - start) >= TEST_TIMEOUT_MS) {
			break;
		}
	}

	if (!s_response_received) {
		ESP_LOGE(TAG, "FAIL: Sensor raw broadcast response not received");

		return false;
	}

	ESP_LOGI(TAG, "PASS: Sensor raw response received");

	if (s_payload_error) {
		ESP_LOGE(TAG, "FAIL: response payload error");

		return false;
	}

	if (!payload_equals(s_last_payload, s_last_length, RESPONSE_PAYLOAD)) {

		ESP_LOGE(TAG, "FAIL: response payload integrity");

		return false;
	}

	ESP_LOGI(TAG, "PASS: response payload integrity");

	return true;
}

#else

static bool run_sensor_test(void) {
	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Sensor waiting for RAW broadcast");

	const uint32_t start = xTaskGetTickCount() * portTICK_PERIOD_MS;

	while (!s_received) {
		vTaskDelay(pdMS_TO_TICKS(50U));

		const uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

		if ((now - start) >= TEST_TIMEOUT_MS) {
			break;
		}
	}

	if (!s_received) {
		ESP_LOGE(TAG, "FAIL: raw broadcast not received");

		return false;
	}

	ESP_LOGI(TAG, "PASS: raw broadcast received");

	if (s_payload_error) {
		ESP_LOGE(TAG, "FAIL: broadcast payload error");

		return false;
	}

	if (!payload_equals(s_last_payload, s_last_length, BROADCAST_PAYLOAD)) {

		ESP_LOGE(TAG, "FAIL: broadcast payload integrity");

		return false;
	}

	ESP_LOGI(TAG, "PASS: broadcast payload integrity");

	return true;
}

#endif

static void deinitialize_raw_espnow(void) {
	esp_err_t err = esp_now_deinit();

	if (err == ESP_OK) {
		ESP_LOGI(TAG, "PASS: raw ESP-NOW deinitialized");
	} else {
		ESP_LOGE(TAG, "FAIL: raw ESP-NOW deinit: %s", esp_err_to_name(err));
	}
}

void app_main(void) {
	bool ok = true;

	if (!initialize_platform()) {
		ok = false;
	}

	if (ok && !initialize_raw_espnow()) {
		ok = false;
	}

	if (ok) {

#if CONFIG_DEVICE_ROLE_GATEWAY
		ok = run_gateway_test();
#else
		ok = run_sensor_test();
#endif
	}

	deinitialize_raw_espnow();

	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY

	if (ok) {
		ESP_LOGI(TAG, "ALL E3.1 RAW GATEWAY BROADCAST "
					  "DIAGNOSTIC TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "E3.1 RAW GATEWAY BROADCAST "
					  "DIAGNOSTIC TEST FAILED");
	}

#else

	if (ok) {
		ESP_LOGI(TAG, "ALL E3.1 RAW SENSOR BROADCAST "
					  "DIAGNOSTIC TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "E3.1 RAW SENSOR BROADCAST "
					  "DIAGNOSTIC TEST FAILED");
	}

#endif

	ESP_LOGI(TAG, "======================================");
}
