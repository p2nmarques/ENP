/*
 * E3.3.7_test_enp_reliability_selftest_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 *
 * E3.3.7 — ENP Reliability Layer Phase 1 Self-Test
 *
 * ESP-IDF 6.0.2 compatible.
 *
 * This test exercises the reliability state machine without ESP-NOW.
 * Hardware integration is intentionally deferred until the core passes.
 */

#include <stdbool.h>

#include "esp_log.h"

#include "core/reliability/enp_reliability.h"

static const char *TAG = "E3_3_7";

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 ENP RELIABILITY CORE SELF-TEST");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "Phase: API / state machine / timeout / retry");
	ESP_LOGI(TAG, "======================================");

	const bool pass = enp_reliability_self_test();

	if (pass) {
		ESP_LOGI(TAG, "PASS: reliable DATA transaction creation");
		ESP_LOGI(TAG, "PASS: ACK timeout detection");
		ESP_LOGI(TAG, "PASS: identical DATA retransmission");
		ESP_LOGI(TAG, "PASS: retry accounting");
		ESP_LOGI(TAG, "PASS: correlated ACK delivery completion");
		ESP_LOGI(TAG, "PASS: duplicate ACK completion suppression");
		ESP_LOGI(TAG, "PASS: retry limit enforcement");
		ESP_LOGI(TAG, "--------------------------------------");
		ESP_LOGI(TAG, "E3.3.7 reliability core self-test PASS");
		ESP_LOGI(TAG, "======================================");
	} else {
		ESP_LOGE(TAG, "--------------------------------------");
		ESP_LOGE(TAG, "E3.3.7 reliability core self-test FAIL");
		ESP_LOGE(TAG, "======================================");
	}
}
