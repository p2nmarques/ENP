/*
 * E3.3.7_p4_E5A_test_enp_next_hop_failure_detection_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5A
 * Next-Hop Failure Detection and Propagation controlled self-test.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Scope:
 *     Validate the transport send-result observation boundary only.
 *
 * Controlled self-test result: PASS / FROZEN (2026-08-17).
 * Hardware validation remains pending.
 *
 * This test intentionally does not exercise route invalidation, RERR,
 * route discovery, route repair, or reliability.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "config/enp_config.h"
#include "core/enp_transport.h"

static const char *TAG = "E3_3_7_P4_E5A";

static enp_transport_send_result_callback_t s_result_callback = NULL;
static void *s_result_context = NULL;

static enp_transport_address_t s_last_destination;
static esp_err_t s_last_result = ESP_FAIL;
static unsigned s_result_count = 0U;

static esp_err_t s_controlled_result = ESP_OK;

static esp_err_t transport_init(const enp_config_t *config) {
	(void)config;
	return ESP_OK;
}

static esp_err_t transport_deinit(void) {
	return ESP_OK;
}

static esp_err_t transport_send(const enp_transport_address_t *destination,
								const void *data, size_t length) {
	(void)data;
	(void)length;

	if ((destination == NULL) || (s_result_callback == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	s_result_callback(destination, s_controlled_result, s_result_context);

	return ESP_OK;
}

static esp_err_t transport_set_receive_callback(
	enp_transport_receive_callback_t callback) {
	(void)callback;
	return ESP_OK;
}

static esp_err_t transport_set_send_result_callback(
	enp_transport_send_result_callback_t callback, void *context) {
	if (callback == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	s_result_callback = callback;
	s_result_context = context;

	return ESP_OK;
}

static enp_transport_t s_transport = {
	.init = transport_init,
	.deinit = transport_deinit,
	.send = transport_send,
	.set_receive_callback = transport_set_receive_callback,
	.set_send_result_callback = transport_set_send_result_callback};

static void send_result_callback(const enp_transport_address_t *destination,
								  esp_err_t result, void *context) {
	(void)context;

	if (destination != NULL) {
		s_last_destination = *destination;
	}

	s_last_result = result;
	++s_result_count;
}

static bool check(bool condition, const char *message, bool *all) {
	if (condition) {
		ESP_LOGI(TAG, "PASS: %s", message);
		return true;
	}

	ESP_LOGE(TAG, "FAIL: %s", message);
	*all = false;
	return false;
}

static void reset_observation(void) {
	memset(&s_last_destination, 0, sizeof(s_last_destination));
	s_last_result = ESP_FAIL;
	s_result_count = 0U;
}

void app_main(void) {
	bool all = true;

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5A");
	ESP_LOGI(TAG, "NEXT-HOP FAILURE DETECTION AND PROPAGATION");
	ESP_LOGI(TAG, "Controlled transport boundary self-test");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	const enp_config_t config = {
		.network_id = 1U,
		.node_id = 1U,
		.role = ENP_ROLE_GATEWAY};

	enp_transport_address_t destination = {
		.value = {0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U},
		.length = 6U};

	static const uint8_t payload[] = {0xE5U, 0xA0U};

	check(enp_transport_init(&s_transport, &config) == ESP_OK,
		  "controlled transport initialized", &all);

	check(enp_transport_set_send_result_callback(
			  &s_transport, send_result_callback, NULL) == ESP_OK,
		  "transport send-result callback registered", &all);

	check(s_transport.set_send_result_callback != NULL,
		  "controlled transport exposes send-result interface", &all);

	reset_observation();
	s_controlled_result = ESP_OK;

	check(enp_transport_send(&s_transport, &destination, payload,
							 sizeof(payload)) == ESP_OK,
		  "controlled SUCCESS transmission submitted", &all);

	check(s_result_count == 1U,
		  "SUCCESS produced exactly one send-result observation", &all);

	check(s_last_result == ESP_OK,
		  "SUCCESS was propagated as ESP_OK", &all);

	check(s_last_destination.length == destination.length &&
			  memcmp(s_last_destination.value, destination.value,
					 destination.length) == 0,
		  "SUCCESS preserved transport destination identity", &all);

	reset_observation();
	s_controlled_result = ESP_FAIL;

	check(enp_transport_send(&s_transport, &destination, payload,
							 sizeof(payload)) == ESP_OK,
		  "controlled FAILURE transmission submitted", &all);

	check(s_result_count == 1U,
		  "FAILURE produced exactly one send-result observation", &all);

	check(s_last_result != ESP_OK,
		  "FAILURE was propagated as non-ESP_OK", &all);

	check(s_last_destination.length == destination.length &&
			  memcmp(s_last_destination.value, destination.value,
					 destination.length) == 0,
		  "FAILURE preserved transport destination identity", &all);

	/*
	 * P4-E5A deliberately stops at transport observation. No route table,
	 * RERR, route discovery, or reliability component is exercised here.
	 */
	check(s_result_count == 1U,
		  "failure observation remained isolated at transport boundary", &all);

	check(enp_transport_deinit(&s_transport) == ESP_OK,
		  "controlled transport deinitialized", &all);

	ESP_LOGI(TAG, "--------------------------------------");
	if (all) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5A self-test PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5A self-test FAIL");
	}
	ESP_LOGI(TAG, "======================================");
}
