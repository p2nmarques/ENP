/*
 * E3.3.7_p4_E5D_step2_test_enp_e5c_repair_event_main.c
 *
 *  Created on: Aug 19, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5D Step 2
 * E5C route failure -> E5D repair-event integration controlled self-test.
 * ESP-IDF 6.0.2 compatible.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "core/enp_transport.h"
#include "core/routing/enp_route_repair.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"
#include "core/routing/enp_route_table.h"

static const char *TAG = "E3_3_7_P4_E5D";

static enp_transport_send_result_callback_t s_send_result_callback;
static void *s_send_result_context;
static bool s_consume_entered;
static volatile bool s_release_consume;
static enp_route_repair_request_t s_consumed[2];
static volatile size_t s_consumed_count;

static enp_transport_address_t make_transport_address(uint8_t last) {
	enp_transport_address_t address = {0};
	address.length = 6U;
	address.value[0] = 0x02U;
	address.value[1] = 0x00U;
	address.value[2] = 0x00U;
	address.value[3] = 0xE5U;
	address.value[4] = 0x0DU;
	address.value[5] = last;
	return address;
}

static bool resolve_transport(void *context, enp_route_destination_t next_hop,
							  enp_transport_address_t *transport_address) {
	(void)context;
	if (transport_address == NULL || next_hop.network_id != 1U ||
		next_hop.node_id == 0U) {
		return false;
	}
	*transport_address = make_transport_address((uint8_t)next_hop.node_id);
	return true;
}

static esp_err_t transport_set_send_result_callback(
	enp_transport_send_result_callback_t callback, void *context) {
	s_send_result_callback = callback;
	s_send_result_context = context;
	return ESP_OK;
}

static esp_err_t transport_send(const enp_transport_address_t *destination,
								const void *data, size_t length) {
	(void)destination;
	(void)data;
	(void)length;
	return ESP_OK;
}

static const enp_transport_t s_transport = {
	.send = transport_send,
	.set_send_result_callback = transport_set_send_result_callback,
};

static enp_route_entry_t make_route(uint16_t destination, uint16_t next_hop,
									uint32_t sequence) {
	enp_route_entry_t entry = {0};
	entry.destination.network_id = 1U;
	entry.destination.node_id = destination;
	entry.next_hop.network_id = 1U;
	entry.next_hop.node_id = next_hop;
	entry.metric.type = ENP_ROUTE_METRIC_HOP_COUNT;
	entry.metric.value = 1U;
	entry.metric.valid = true;
	entry.route_sequence = sequence;
	entry.state = ENP_ROUTE_STATE_ACTIVE;
	return entry;
}

static const enp_route_entry_t *find_route(const enp_route_table_t *routes,
										   uint16_t destination) {
	for (size_t i = 0U; i < routes->count; ++i) {
		if (routes->entries[i].destination.node_id == destination) {
			return &routes->entries[i];
		}
	}
	return NULL;
}

static void check(bool condition, const char *message, bool *all) {
	if (condition) {
		ESP_LOGI(TAG, "PASS: %s", message);
	} else {
		ESP_LOGE(TAG, "FAIL: %s", message);
		*all = false;
	}
}

static void consume_request(const enp_route_repair_request_t *request,
							void *context) {
	(void)context;
	if (request == NULL) {
		return;
	}

	s_consume_entered = true;
	if (s_consumed_count < 2U) {
		s_consumed[s_consumed_count] = *request;
	}

	while (!s_release_consume) {
		vTaskDelay(pdMS_TO_TICKS(10U));
	}

	++s_consumed_count;
}

static void route_failure_callback(void *context,
								   enp_route_destination_t destination,
								   enp_route_destination_t failed_next_hop) {
	enp_route_repair_t *repair = (enp_route_repair_t *)context;
	(void)enp_route_repair_request(repair, destination, failed_next_hop);
}

static void emit_failure(const enp_transport_address_t *destination) {
	if (s_send_result_callback != NULL) {
		s_send_result_callback(destination, ESP_FAIL, s_send_result_context);
	}
}

void app_main(void) {
	bool all = true;

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5D STEP 2");
	ESP_LOGI(TAG, "E5C ROUTE FAILURE -> E5D REPAIR EVENT");
	ESP_LOGI(TAG, "Controlled integration self-test");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

    /*
     * These objects own persistent FreeRTOS/static resources. They must not
     * live on app_main()'s automatic stack because the E5D task outlives the
     * local initialization scope.
     */
    static enp_route_table_t routes;
    static enp_routing_data_path_t path;
    static enp_route_repair_t repair;

	check(enp_route_table_init(&routes), "route table initialized", &all);
	enp_route_entry_t route_a = make_route(10U, 2U, 0x1001U);
	enp_route_entry_t route_b = make_route(11U, 2U, 0x1002U);
	enp_route_entry_t route_c = make_route(12U, 3U, 0x1003U);
	check(enp_route_table_insert(&routes, &route_a),
		  "route A installed via failed next-hop 2", &all);
	check(enp_route_table_insert(&routes, &route_b),
		  "route B installed via failed next-hop 2", &all);
	check(enp_route_table_insert(&routes, &route_c),
		  "unrelated route C installed via next-hop 3", &all);

	check(enp_route_repair_init(&repair, consume_request, NULL),
		  "E5D repair coordinator initialized", &all);
	check(enp_routing_data_path_init(&path, &routes,
									 (enp_transport_t *)&s_transport,
									 resolve_transport, NULL),
		  "routing data path initialized", &all);
	check(enp_routing_data_path_set_route_failure_callback(
			  &path, route_failure_callback, &repair),
		  "E5C -> E5D route-failure callback registered", &all);
	check(s_send_result_callback != NULL && s_send_result_context == &path,
		  "production transport callback remains bound to routing path", &all);

	s_release_consume = false;
	s_consume_entered = false;
	s_consumed_count = 0U;

	const enp_transport_address_t failed_next_hop = make_transport_address(2U);
	emit_failure(&failed_next_hop);

	for (int i = 0; i < 100 && !s_consume_entered; ++i) {
		vTaskDelay(pdMS_TO_TICKS(10U));
	}

	check(s_consume_entered,
		  "first affected route produced an E5D repair event", &all);
	check(find_route(&routes, 10U)->state == ENP_ROUTE_STATE_STALE,
		  "route A became STALE through E5C", &all);
	check(find_route(&routes, 11U)->state == ENP_ROUTE_STATE_STALE,
		  "route B became STALE through E5C", &all);
	check(find_route(&routes, 12U)->state == ENP_ROUTE_STATE_ACTIVE,
		  "unrelated route C remained ACTIVE", &all);

	check(enp_route_repair_pending_count(&repair) == 2U,
		  "both affected destinations are represented by bounded E5D repair "
		  "state",
		  &all);
	check(enp_route_repair_request_count(&repair) == 2U,
		  "E5C generated exactly one repair request per affected route", &all);

	check(s_consumed[0].destination.network_id == 1U &&
			  s_consumed[0].destination.node_id == 10U &&
			  s_consumed[0].failed_next_hop.node_id == 2U,
		  "first repair event preserved destination and failed next-hop", &all);

	s_release_consume = true;
	for (int i = 0; i < 100 && s_consumed_count < 2U; ++i) {
		vTaskDelay(pdMS_TO_TICKS(10U));
	}

	check(s_consumed_count == 2U,
		  "exactly two affected-route repair events were consumed", &all);
	check(enp_route_repair_suppressed_count(&repair) == 0U,
		  "no unrelated repair request was generated", &all);

	ESP_LOGI(TAG, "--------------------------------------");
	if (all) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5D Step 2 self-test PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5D Step 2 self-test FAIL");
	}
	ESP_LOGI(TAG, "======================================");
}
