/*
 * E3.3.7_p4_E5B_test_enp_real_espnow_tx_result_main.c
 *
 *  Created on: Aug 18, 2026
 *      Author: Pedro Marques
 */

/*
 * E3.3.7_p4_E5B_test_enp_real_espnow_tx_result_main.c
 *
 * E3.3.7 Phase 4 / P4-E5B
 * Real ESP-NOW TX-result observation hardware validation.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Scope:
 *     Validate the production ESP-NOW send-result callback path on real
 *     ESP32 hardware.
 *
 * Test condition:
 *     A unicast peer address is configured which must NOT be powered,
 *     associated with the test network, or present as another ESP-NOW node
 *     during the test. The ESP-NOW API must accept the transmission request,
 *     and the real ESP-NOW send callback must subsequently report failure.
 *
 * This test intentionally does not exercise:
 *     - route invalidation
 *     - RERR
 *     - RREQ/RREP
 *     - route discovery
 *     - route repair
 *     - reliability
 *
 * The test validates only the frozen P4-E5A transport observation boundary.
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
#include "core/enp_transport.h"
#include "link/enp_transport_espnow.h"
#include "link/enp_transport_wifi.h"

/*----------------------------------------------------------
 * Configuration
 *---------------------------------------------------------*/

/*
 * IMPORTANT:
 *
 * This MAC must not belong to any powered ESP32 or other active
 * ESP-NOW peer during the test.
 *
 * Change it if this address is already in use in the test environment.
 */
#define P4_E5B_UNREACHABLE_MAC {0x02U, 0x00U, 0x00U, 0xE5U, 0x0BU, 0x5BU}

#define P4_E5B_WAIT_MS 10000U

#define P4_E5B_PAYLOAD "ENP-P4-E5B-TX-RESULT"
#define P4_E5B_CORRELATION_ID ((enp_transport_correlation_id_t)0xE5B001U)

/*----------------------------------------------------------
 * Logging
 *---------------------------------------------------------*/

static const char *TAG = "E3_3_7_P4_E5B";

/*----------------------------------------------------------
 * Runtime State
 *---------------------------------------------------------*/

static enp_context_t s_context;

static volatile bool s_callback_received = false;

static volatile bool s_test_complete = false;

static volatile unsigned s_callback_count = 0U;

static volatile esp_err_t s_callback_result = ESP_OK;
static volatile enp_transport_correlation_id_t s_callback_correlation_id = ENP_TRANSPORT_INVALID_CORRELATION_ID;

static enp_transport_address_t s_callback_destination;

static char s_callback_task_name[configMAX_TASK_NAME_LEN];

/*----------------------------------------------------------
 * Helpers
 *---------------------------------------------------------*/

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

		if ((xTaskGetTickCount() - start) >= pdMS_TO_TICKS(P4_E5B_WAIT_MS)) {
			return false;
		}
	}

	return true;
}

static enp_config_t make_config(void) {
	enp_config_t config = {0};

	config.network_id = 1U;

#if CONFIG_DEVICE_ROLE_GATEWAY
	config.node_id = 1U;
	config.role = ENP_ROLE_GATEWAY;
#else
	config.node_id = 2U;
	config.role = ENP_ROLE_SENSOR;
#endif

	return config;
}

static enp_transport_address_t make_unreachable_address(void) {
	static const uint8_t mac[6] = P4_E5B_UNREACHABLE_MAC;

	enp_transport_address_t address = {0};

	address.length = 6U;

	memcpy(address.value, mac, 6U);

	return address;
}

static bool address_equal(const enp_transport_address_t *a,
						  const enp_transport_address_t *b) {
	if ((a == NULL) || (b == NULL)) {
		return false;
	}

	if (a->length != b->length) {
		return false;
	}

	return memcmp(a->value, b->value, a->length) == 0;
}

static void log_address(const char *label,
						const enp_transport_address_t *address) {
	if ((address == NULL) || (address->length != 6U)) {
		ESP_LOGI(TAG, "%s: invalid transport address", label);
		return;
	}

	ESP_LOGI(TAG, "%s: %02X:%02X:%02X:%02X:%02X:%02X", label, address->value[0],
			 address->value[1], address->value[2], address->value[3],
			 address->value[4], address->value[5]);
}

/*----------------------------------------------------------
 * Real transport send-result callback
 *---------------------------------------------------------*/

static void send_result_callback(const enp_transport_address_t *destination,
								 esp_err_t result, void *context) {
	(void)context;

	if (destination != NULL) {
		s_callback_destination = *destination;
	}

	s_callback_result = result;

	++s_callback_count;

	strncpy(s_callback_task_name, pcTaskGetName(NULL),
			sizeof(s_callback_task_name) - 1U);
	s_callback_task_name[sizeof(s_callback_task_name) - 1U] = '\0';

	s_callback_received = true;
	s_test_complete = true;

	ESP_LOGI(TAG, "TX-result callback observed: result=%s task=%s",
			 esp_err_to_name(result), s_callback_task_name);

	log_address("TX-result destination", destination);
}

static void send_result_ex_callback(
	const enp_transport_address_t *destination, esp_err_t result,
	enp_transport_correlation_id_t correlation_id, void *context) {
	(void)context;
	if (destination != NULL) s_callback_destination = *destination;
	s_callback_correlation_id = correlation_id;
	ESP_LOGI(TAG, "Correlated TX-result: result=%s correlation=0x%08" PRIX32,
			esp_err_to_name(result), correlation_id);
}

/*----------------------------------------------------------
 * Test
 *---------------------------------------------------------*/

void app_main(void) {
	bool pass = true;

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5B");
	ESP_LOGI(TAG, "REAL ESP-NOW TX-RESULT OBSERVATION");
	ESP_LOGI(TAG, "Hardware validation");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

#if CONFIG_DEVICE_ROLE_GATEWAY
	ESP_LOGI(TAG, "Hardware role: GATEWAY / NODE 1");
#else
	ESP_LOGI(TAG, "Hardware role: SENSOR / NODE 2");
#endif

	/*
	 * The test requires a real Wi-Fi/ESP-NOW transport.
	 */
	nvs_init();

	if (esp_netif_init() != ESP_OK) {
		ESP_LOGE(TAG, "FAIL: ESP-NETIF initialization");
		pass = false;
	}

	if (pass && (esp_event_loop_create_default() != ESP_OK)) {
		ESP_LOGE(TAG, "FAIL: default event loop initialization");
		pass = false;
	}

	if (pass && (enp_wifi_init() != ESP_OK)) {
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

	const enp_transport_address_t unreachable = make_unreachable_address();

	if (pass) {
		log_address("Configured unreachable next-hop", &unreachable);
		ESP_LOGI(TAG, "IMPORTANT: verify that this MAC is not an active peer");
	}

	enp_transport_t *transport = NULL;

	if (pass) {
		transport = enp_transport_espnow_get();

		if (transport == NULL) {
			ESP_LOGE(TAG, "FAIL: ESP-NOW transport instance unavailable");
			pass = false;
		}
	}

	if (pass) {
		const enp_config_t config = make_config();

		if (enp_context_init(&s_context, transport, &config) != ESP_OK) {
			ESP_LOGE(TAG, "FAIL: real ESP-NOW transport initialization");
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: real ESP-NOW transport initialized");
		}
	}

	if (pass) {
		if (enp_transport_set_send_result_callback(
				s_context.transport, send_result_callback, NULL) != ESP_OK) {
			ESP_LOGE(TAG, "FAIL: production send-result callback registration");
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: production send-result callback registered");
		}
	}

	if (pass) {
		if (enp_transport_set_send_result_callback_ex(
			s_context.transport, send_result_ex_callback, NULL) != ESP_OK) {
			ESP_LOGE(TAG, "FAIL: correlated send-result callback registration");
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: correlated send-result callback registered");
		}
	}

	if (pass) {
		static const uint8_t payload[] = P4_E5B_PAYLOAD;

		const esp_err_t submit_result = enp_transport_send_ex(
			s_context.transport, &unreachable, payload, sizeof(payload) - 1U,
			P4_E5B_CORRELATION_ID);

		if (submit_result != ESP_OK) {
			/*
			 * An immediate API error is not the E5B asynchronous
			 * observation being tested.
			 */
			ESP_LOGE(TAG, "FAIL: real ESP-NOW send submission returned %s",
					 esp_err_to_name(submit_result));
			pass = false;
		} else {
			ESP_LOGI(
				TAG,
				"PASS: real ESP-NOW transmission submitted asynchronously");
		}
	}

	if (pass) {
		const TickType_t start = xTaskGetTickCount();

		while (!s_test_complete && ((xTaskGetTickCount() - start) <
									pdMS_TO_TICKS(P4_E5B_WAIT_MS))) {
			vTaskDelay(pdMS_TO_TICKS(50U));
		}

		if (!s_callback_received) {
			ESP_LOGE(TAG,
					 "FAIL: real ESP-NOW TX-result callback was not observed");
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: real ESP-NOW TX-result callback was observed");
		}
	}

	if (pass) {
		if (s_callback_correlation_id != P4_E5B_CORRELATION_ID) {
			ESP_LOGE(TAG, "FAIL: correlation mismatch expected=0x%08" PRIX32 " actual=0x%08" PRIX32,
					(uint32_t)P4_E5B_CORRELATION_ID, (uint32_t)s_callback_correlation_id);
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: transport correlation preserved through ESP-NOW callback");
		}
	}

	if (pass) {
		if (s_callback_count != 1U) {
			ESP_LOGE(TAG,
					 "FAIL: expected exactly one TX-result callback, got %u",
					 s_callback_count);
			pass = false;
		} else {
			ESP_LOGI(TAG,
					 "PASS: exactly one real TX-result observation received");
		}
	}

	if (pass) {
		if (s_callback_result == ESP_OK) {
			ESP_LOGE(TAG,
					 "FAIL: configured unreachable next-hop reported SUCCESS");
			pass = false;
		} else {
			ESP_LOGI(TAG,
					 "PASS: real ESP-NOW reported transmission FAILURE: %s",
					 esp_err_to_name(s_callback_result));
		}
	}

	if (pass) {
		if (!address_equal(&s_callback_destination, &unreachable)) {
			ESP_LOGE(TAG,
					 "FAIL: TX-result callback destination identity mismatch");
			log_address("Expected destination", &unreachable);
			log_address("Observed destination", &s_callback_destination);
			pass = false;
		} else {
			ESP_LOGI(TAG,
					 "PASS: real TX-result preserved destination identity");
		}
	}

	if (pass) {
		if (s_callback_task_name[0] == '\0') {
			ESP_LOGE(
				TAG,
				"FAIL: TX-result callback execution context not observable");
			pass = false;
		} else {
			ESP_LOGI(TAG,
					 "PASS: TX-result callback executed in task context: %s",
					 s_callback_task_name);
		}
	}

	if (s_context.transport != NULL) {
		if (enp_context_deinit(&s_context) != ESP_OK) {
			ESP_LOGE(TAG, "FAIL: real ESP-NOW transport deinitialization");
			pass = false;
		} else {
			ESP_LOGI(TAG, "PASS: real ESP-NOW transport deinitialized");
		}
	}

	ESP_LOGI(TAG, "--------------------------------------");

	if (pass) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5B hardware validation PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5B hardware validation FAIL");
	}

	ESP_LOGI(TAG, "======================================");

	vTaskDelete(NULL);
}
