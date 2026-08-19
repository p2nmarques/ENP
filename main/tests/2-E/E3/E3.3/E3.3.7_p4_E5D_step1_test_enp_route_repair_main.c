/*
 * E3.3.7_p4_E5D_step1_test_enp_route_repair_main.c
 *
 *  Created on: Aug 19, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5D
 * Step 1 — Route Repair Coordinator Controlled Self-Test
 *
 * This test intentionally validates only:
 *   - static initialization;
 *   - bounded request submission;
 *   - duplicate suppression while a request is being consumed;
 *   - task-context consumption.
 *
 * R4, route-table, E5C, reliability and transport integration are not tested
 * here and are intentionally not connected yet.
 */

#include "core/routing/enp_route_repair.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"

static const char *TAG = "E3_3_7_P4_E5D";

typedef struct {
	StaticSemaphore_t entered_control;
	SemaphoreHandle_t entered;
	StaticSemaphore_t release_control;
	SemaphoreHandle_t release;
	uint32_t consumed;
	enp_route_repair_request_t last_request;
} test_context_t;

static void consume_callback(const enp_route_repair_request_t *request,
							 void *context) {
	test_context_t *test = (test_context_t *)context;
	if (test == NULL || request == NULL) {
		return;
	}

	test->last_request = *request;
	++test->consumed;
	(void)xSemaphoreGive(test->entered);
	(void)xSemaphoreTake(test->release, portMAX_DELAY);
}

static bool wait_for_sem(SemaphoreHandle_t semaphore, TickType_t timeout) {
	return xSemaphoreTake(semaphore, timeout) == pdTRUE;
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5D STEP 1");
	ESP_LOGI(TAG, "ROUTE REPAIR COORDINATOR");
	ESP_LOGI(TAG, "Controlled self-test");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	test_context_t test = {0};
	test.entered = xSemaphoreCreateBinaryStatic(&test.entered_control);
	test.release = xSemaphoreCreateBinaryStatic(&test.release_control);

	enp_route_repair_t repair = {0};

	bool pass = enp_route_repair_init(&repair, consume_callback, &test);
	ESP_LOGI(TAG, "%s: repair coordinator initialized with static resources",
			 pass ? "PASS" : "FAIL");

	enp_route_destination_t destination = {.network_id = 1U, .node_id = 10U};
	enp_route_destination_t failed_next_hop = {.network_id = 1U, .node_id = 2U};

	bool accepted =
		enp_route_repair_request(&repair, destination, failed_next_hop);
	ESP_LOGI(TAG, "%s: first repair request accepted",
			 accepted ? "PASS" : "FAIL");

	bool consumed = wait_for_sem(test.entered, pdMS_TO_TICKS(1000U));
	ESP_LOGI(TAG, "%s: repair request reached dedicated task context",
			 consumed ? "PASS" : "FAIL");

	bool duplicate =
		enp_route_repair_request(&repair, destination, failed_next_hop);
	ESP_LOGI(TAG, "%s: duplicate repair request suppressed while active",
			 !duplicate ? "PASS" : "FAIL");

	ESP_LOGI(TAG, "%s: destination identity preserved",
			 test.last_request.destination.node_id == destination.node_id
				 ? "PASS"
				 : "FAIL");
	ESP_LOGI(TAG, "%s: failed-next-hop identity preserved",
			 test.last_request.failed_next_hop.node_id ==
					 failed_next_hop.node_id
				 ? "PASS"
				 : "FAIL");

	(void)xSemaphoreGive(test.release);
	vTaskDelay(pdMS_TO_TICKS(50U));

	ESP_LOGI(TAG, "%s: exactly one repair request consumed",
			 test.consumed == 1U ? "PASS" : "FAIL");

	ESP_LOGI(TAG, "%s: duplicate suppression count is exactly one",
			 enp_route_repair_suppressed_count(&repair) == 1U ? "PASS"
															  : "FAIL");

	ESP_LOGI(TAG, "--------------------------------------");
	bool all_pass = pass && accepted && consumed && !duplicate &&
					test.consumed == 1U &&
					enp_route_repair_suppressed_count(&repair) == 1U;
	ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5D Step 1 self-test %s",
			 all_pass ? "PASS" : "FAIL");
	ESP_LOGI(TAG, "======================================");
}
