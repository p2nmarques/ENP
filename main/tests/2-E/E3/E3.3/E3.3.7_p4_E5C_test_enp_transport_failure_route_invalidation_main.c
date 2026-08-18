/*
 * E3.3.7_p4_E5C_test_enp_transport_failure_route_invalidation_main.c
 *
 *  Created on: Aug 18, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E5C
 * Transport Failure -> Route Invalidation controlled self-test.
 *
 * Target: ESP-IDF 6.0.2
 *
 * Scope:
 *     Validate the routing integration boundary that consumes the frozen
 *     P4-E5B transport send-result observation and invalidates every ACTIVE
 *     route whose logical next-hop resolves to the failed transport address.
 *
 * This controlled test intentionally does not exercise:
 *     - RERR generation or processing
 *     - RREQ/RREP
 *     - route discovery
 *     - route repair
 *     - reliability recovery
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "config/enp_config.h"
#include "core/enp_transport.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"

static const char *TAG = "E3_3_7_P4_E5C";

static enp_transport_send_result_callback_t s_send_result_callback = NULL;
static void *s_send_result_context = NULL;
static esp_err_t s_send_result = ESP_OK;

static esp_err_t transport_init(const enp_config_t *config) {
	(void)config;
	return ESP_OK;
}

static esp_err_t transport_deinit(void) {
	return ESP_OK;
}

static esp_err_t transport_send(const enp_transport_address_t *destination,
								const void *data, size_t length) {
	(void)destination;
	(void)data;
	(void)length;
	return ESP_OK;
}

static esp_err_t transport_set_receive_callback(
		enp_transport_receive_callback_t callback) {
	return callback != NULL ? ESP_OK : ESP_ERR_INVALID_ARG;
}

static esp_err_t transport_set_send_result_callback(
		enp_transport_send_result_callback_t callback, void *context) {
	if (callback == NULL || context == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	s_send_result_callback = callback;
	s_send_result_context = context;

	return ESP_OK;
}

static enp_transport_t s_transport = {
	.init = transport_init,
	.deinit = transport_deinit,
	.send = transport_send,
	.set_receive_callback = transport_set_receive_callback,
	.set_send_result_callback = transport_set_send_result_callback};

static enp_transport_address_t make_mac(uint8_t last_octet) {
	enp_transport_address_t address = {0};
	address.length = 6U;
	address.value[0] = 0x02U;
	address.value[1] = 0x00U;
	address.value[2] = 0x00U;
	address.value[3] = 0xE5U;
	address.value[4] = 0x0CU;
	address.value[5] = last_octet;
	return address;
}

static bool resolve_transport(void *context,
						  enp_route_destination_t next_hop,
						  enp_transport_address_t *address) {
	(void)context;
	if (address == NULL || next_hop.network_id != 1U) {
		return false;
	}

	if (next_hop.node_id == 2U) {
		*address = make_mac(0x02U);
		return true;
	}

	if (next_hop.node_id == 3U) {
		*address = make_mac(0x03U);
		return true;
	}

	return false;
}

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
	entry.expires_at_ms = 60000U;
	entry.state = ENP_ROUTE_STATE_ACTIVE;
	return entry;
}

static const enp_route_entry_t *find_entry(const enp_route_table_t *table,
									uint16_t node);

static bool route_state(const enp_route_table_t *table, uint16_t node,
						enp_route_state_t expected) {
	const enp_route_entry_t *entry = find_entry(table, node);
	return entry != NULL && entry->state == expected;
}

static const enp_route_entry_t *find_entry(const enp_route_table_t *table,
										uint16_t node) {
	if (table == NULL) {
		return NULL;
	}
	for (size_t i = 0U; i < table->count; ++i) {
		if (table->entries[i].destination.network_id == 1U &&
			table->entries[i].destination.node_id == node) {
			return &table->entries[i];
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

static void emit_result(const enp_transport_address_t *destination,
						esp_err_t result) {
	if (s_send_result_callback != NULL) {
		s_send_result_callback(destination, result, s_send_result_context);
	}
}

void app_main(void) {
	bool all = true;

	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4-E5C ROUTE INVALIDATION");
	ESP_LOGI(TAG, "Transport Failure -> Route Invalidation");
	ESP_LOGI(TAG, "Controlled routing integration self-test");
	ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
	ESP_LOGI(TAG, "======================================");

	enp_route_table_t routes;
	enp_routing_data_path_t path = {0};

	const enp_config_t config = {
		.network_id = 1U,
		.node_id = 1U,
		.role = ENP_ROLE_GATEWAY};

	check(enp_transport_init(&s_transport, &config) == ESP_OK,
		  "controlled transport initialized", &all);

	check(enp_route_table_init(&routes), "route table initialized", &all);

	const enp_route_entry_t route_a = make_route(10U, 2U, 0x1001U);
	const enp_route_entry_t route_b = make_route(11U, 2U, 0x1002U);
	const enp_route_entry_t route_c = make_route(12U, 3U, 0x1003U);

	check(enp_route_table_insert(&routes, &route_a),
		  "route A installed: destination 10 via next-hop 2", &all);
	check(enp_route_table_insert(&routes, &route_b),
		  "route B installed: destination 11 via next-hop 2", &all);
	check(enp_route_table_insert(&routes, &route_c),
		  "route C installed: destination 12 via next-hop 3", &all);

	check(enp_routing_data_path_init(&path, &routes, &s_transport,
								resolve_transport, NULL),
		  "routing data path initialized with failure integration", &all);
/* Verify the actual controlled transport binding. */
	check(s_send_result_callback != NULL,
		  "routing data path registered production send-result callback", &all);
	check(s_send_result_context == &path,
		  "transport callback is bound to routing data path context", &all);

	check(route_state(&routes, 10U, ENP_ROUTE_STATE_ACTIVE),
		  "route A initially ACTIVE", &all);
	check(route_state(&routes, 11U, ENP_ROUTE_STATE_ACTIVE),
		  "route B initially ACTIVE", &all);
	check(route_state(&routes, 12U, ENP_ROUTE_STATE_ACTIVE),
		  "route C initially ACTIVE", &all);

	const enp_route_entry_t *a_before = find_entry(&routes, 10U);
	const enp_route_entry_t *b_before = find_entry(&routes, 11U);
	const uint32_t seq_a = a_before != NULL ? a_before->route_sequence : 0U;
	const uint32_t seq_b = b_before != NULL ? b_before->route_sequence : 0U;

	const enp_transport_address_t failed_next_hop = make_mac(0x02U);
	s_send_result = ESP_FAIL;
	emit_result(&failed_next_hop, s_send_result);

	check(route_state(&routes, 10U, ENP_ROUTE_STATE_STALE),
		  "failed next-hop invalidated route A", &all);
	check(route_state(&routes, 11U, ENP_ROUTE_STATE_STALE),
		  "failed next-hop invalidated route B sharing the same next-hop", &all);
	check(route_state(&routes, 12U, ENP_ROUTE_STATE_ACTIVE),
		  "unrelated route C remained ACTIVE", &all);

	const enp_route_entry_t *a_after = find_entry(&routes, 10U);
	const enp_route_entry_t *b_after = find_entry(&routes, 11U);
	check(a_after != NULL && a_after->route_sequence == seq_a,
		  "route A sequence preserved during invalidation", &all);
	check(b_after != NULL && b_after->route_sequence == seq_b,
		  "route B sequence preserved during invalidation", &all);

	/* A successful result must never invalidate the matching next-hop routes. */
	s_send_result = ESP_OK;
	emit_result(&failed_next_hop, s_send_result);
	check(route_state(&routes, 12U, ENP_ROUTE_STATE_ACTIVE),
		  "successful TX result left unrelated active route unchanged", &all);

	/* Repeated failure is intentionally idempotent. */
	s_send_result = ESP_FAIL;
	emit_result(&failed_next_hop, s_send_result);
	check(find_entry(&routes, 10U) != NULL &&
			  find_entry(&routes, 10U)->state == ENP_ROUTE_STATE_STALE,
		  "repeated failure kept route A STALE", &all);
	check(find_entry(&routes, 11U) != NULL &&
			  find_entry(&routes, 11U)->state == ENP_ROUTE_STATE_STALE,
		  "repeated failure kept route B STALE", &all);

	/* A failure for another transport address must not affect route A/B. */
	const enp_transport_address_t other_next_hop = make_mac(0x03U);
	emit_result(&other_next_hop, ESP_FAIL);
	check(find_entry(&routes, 12U) != NULL &&
			  find_entry(&routes, 12U)->state == ENP_ROUTE_STATE_STALE,
		  "failure of next-hop 3 invalidated its route C", &all);
	check(enp_route_table_active_count(&routes) == 0U,
		  "all and only affected routes are now STALE", &all);

	check(enp_transport_deinit(&s_transport) == ESP_OK,
		  "controlled transport deinitialized", &all);

	ESP_LOGI(TAG, "--------------------------------------");
	if (all) {
		ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4-E5C self-test PASS");
	} else {
		ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4-E5C self-test FAIL");
	}
	ESP_LOGI(TAG, "======================================");
}
 