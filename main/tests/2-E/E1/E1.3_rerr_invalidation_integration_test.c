/*
 * E1.3_rerr_invalidation_integration_test.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E1.3 RERR invalidation + stale RERR integration test.
 *
 * E1 is hardware-independent:
 *   R4-D RERR processor + R3-B route table + R3-A metric.
 *
 * Route reuse is deliberately NOT tested here. The frozen E1 specification
 * places route reuse under E3/ESP-NOW integration. E1 tests RERR invalidation
 * and stale-RERR protection.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "esp_log.h"

#include "core/protocol/payloads/enp_routing.h"
#include "core/routing/enp_rerr_processor.h"
#include "core/routing/enp_route_table.h"

static const char *TAG = "E1RERR";
static int s_failures;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)
#define FAIL(name)                                                             \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", name);                                       \
		++s_failures;                                                          \
	} while (0)

#define EXPECT_TRUE(condition, name)                                           \
	do {                                                                       \
		if (condition) {                                                       \
			PASS(name);                                                        \
		} else {                                                               \
			FAIL(name);                                                        \
		}                                                                      \
	} while (0)

typedef struct {
	enp_route_table_t table;
	uint32_t lookup_count;
	uint32_t invalidate_count;
} node_context_t;

/*
 * Route tables are large; keep test state out of the main-task stack.
 */
static node_context_t s_node_a;

static enp_route_destination_t destination(uint16_t network_id,
										   uint16_t node_id) {
	return (enp_route_destination_t){.network_id = network_id,
									 .node_id = node_id};
}

static bool make_metric(uint16_t hops, enp_route_metric_t *metric) {
	if (metric == NULL) {
		return false;
	}

	if (!enp_route_metric_init(metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
		return false;
	}

	metric->value = hops;
	metric->valid = true;

	return true;
}

static bool install_route(node_context_t *ctx, uint16_t destination_node,
						  uint16_t next_hop_node, uint32_t sequence) {
	enp_route_entry_t entry = {0};

	entry.destination = destination(1U, destination_node);
	entry.next_hop = destination(1U, next_hop_node);
	entry.route_sequence = sequence;
	entry.expires_at_ms = 60000U;
	entry.state = ENP_ROUTE_STATE_ACTIVE;

	if (!make_metric(1U, &entry.metric)) {
		return false;
	}

	return enp_route_table_insert(&ctx->table, &entry);
}

static bool mock_lookup_route(void *context,
							  enp_rerr_destination_t rerr_destination,
							  enp_rerr_route_info_t *route_info) {
	node_context_t *ctx = context;

	if (ctx == NULL || route_info == NULL) {
		return false;
	}

	++ctx->lookup_count;

	const enp_route_entry_t *entry = enp_route_table_lookup_const(
		&ctx->table,
		destination(rerr_destination.network_id, rerr_destination.node_id));

	if (entry == NULL) {
		route_info->installed = false;
		route_info->active = false;
		route_info->destination_sequence = 0U;
		return true;
	}

	route_info->installed = true;
	route_info->active = entry->state == ENP_ROUTE_STATE_ACTIVE;
	route_info->destination_sequence = entry->route_sequence;

	return true;
}

static bool mock_invalidate_route(void *context,
								  enp_rerr_destination_t rerr_destination) {
	node_context_t *ctx = context;

	if (ctx == NULL) {
		return false;
	}

	++ctx->invalidate_count;

	return enp_route_table_invalidate(
		&ctx->table,
		destination(rerr_destination.network_id, rerr_destination.node_id));
}

static bool init_processor(enp_rerr_processor_t *processor,
						   node_context_t *ctx) {
	enp_rerr_processor_callbacks_t callbacks = {
		.context = ctx,
		.lookup_route = mock_lookup_route,
		.invalidate_route = mock_invalidate_route};

	return enp_rerr_processor_init(processor, &callbacks);
}

static enp_routing_rerr_t make_rerr(uint16_t destination_node,
									uint32_t sequence, uint8_t reason) {
	return (enp_routing_rerr_t){.payload_version = ENP_ROUTING_PAYLOAD_VERSION,
								.subtype = ENP_ROUTING_SUBTYPE_RERR,
								.unreachable_network_id = 1U,
								.unreachable_node_id = destination_node,
								.destination_sequence = sequence,
								.reason = reason,
								.reserved_0 = 0U,
								.reserved_1 = 0U};
}

static void reset_node(void) {
	memset(&s_node_a, 0, sizeof(s_node_a));

	(void)enp_route_table_init(&s_node_a.table);
}

static void test_newer_rerr_invalidates_route(void) {
	enp_rerr_processor_t processor;

	reset_node();

	EXPECT_TRUE(install_route(&s_node_a, 3U, 2U, 100U),
				"A installs active route to C");

	EXPECT_TRUE(init_processor(&processor, &s_node_a),
				"A RERR processor initialized");

	enp_routing_rerr_t rerr =
		make_rerr(3U, 101U, ENP_ROUTE_ERROR_NEXT_HOP_UNREACHABLE);

	enp_rerr_result_t result = enp_rerr_processor_handle(&processor, &rerr);

	EXPECT_TRUE(result == ENP_RERR_RESULT_INVALIDATED,
				"newer RERR invalidates A route to C");

	EXPECT_TRUE(s_node_a.lookup_count == 1U,
				"newer RERR performs one route lookup");

	EXPECT_TRUE(s_node_a.invalidate_count == 1U,
				"newer RERR performs one route invalidation");

	/*
	 * IMPORTANT:
	 *
	 * enp_route_table_lookup_const() returns only an ACTIVE route.
	 * After invalidation the entry must remain in the fixed table,
	 * but its state becomes STALE.
	 *
	 * Therefore inspect the table storage directly here.
	 */
	EXPECT_TRUE(s_node_a.table.count == 1U,
				"invalidated route remains in route table");

	const enp_route_entry_t *route = NULL;

	for (size_t i = 0; i < s_node_a.table.count; ++i) {
		if (s_node_a.table.entries[i].destination.network_id == 1U &&
			s_node_a.table.entries[i].destination.node_id == 3U) {
			route = &s_node_a.table.entries[i];
			break;
		}
	}

	EXPECT_TRUE(route != NULL, "invalidated route entry is retained");

	if (route != NULL) {
		EXPECT_TRUE(route->state == ENP_ROUTE_STATE_STALE,
					"invalidated route becomes STALE");

		EXPECT_TRUE(route->next_hop.node_id == 2U,
					"invalidated route retains next hop B");
	}

	EXPECT_TRUE(enp_route_table_active_count(&s_node_a.table) == 0U,
				"invalidated route is no longer active");
}

static void test_stale_rerr_does_not_invalidate(void) {
	enp_rerr_processor_t processor;

	reset_node();

	EXPECT_TRUE(install_route(&s_node_a, 3U, 2U, 100U),
				"A installs route for stale-RERR test");

	EXPECT_TRUE(init_processor(&processor, &s_node_a),
				"A RERR processor initialized for stale test");

	enp_routing_rerr_t rerr = make_rerr(3U, 99U, ENP_ROUTE_ERROR_NO_ROUTE);

	enp_rerr_result_t result = enp_rerr_processor_handle(&processor, &rerr);

	EXPECT_TRUE(result == ENP_RERR_RESULT_IGNORED_STALE,
				"older RERR is ignored");

	EXPECT_TRUE(s_node_a.lookup_count == 1U,
				"stale RERR performs one route lookup");

	EXPECT_TRUE(s_node_a.invalidate_count == 0U,
				"stale RERR does not invalidate route");

	const enp_route_entry_t *route =
		enp_route_table_lookup_const(&s_node_a.table, destination(1U, 3U));

	EXPECT_TRUE(route != NULL && route->state == ENP_ROUTE_STATE_ACTIVE,
				"route remains active after stale RERR");
}

static void test_unknown_rerr_sequence_against_known_route(void) {
	enp_rerr_processor_t processor;

	reset_node();

	EXPECT_TRUE(install_route(&s_node_a, 3U, 2U, 100U),
				"A installs route for unknown-sequence test");

	EXPECT_TRUE(init_processor(&processor, &s_node_a),
				"A RERR processor initialized for unknown-sequence test");

	enp_routing_rerr_t rerr = make_rerr(3U, 0U, ENP_ROUTE_ERROR_ROUTE_EXPIRED);

	enp_rerr_result_t result = enp_rerr_processor_handle(&processor, &rerr);

	EXPECT_TRUE(result == ENP_RERR_RESULT_IGNORED_STALE,
				"unknown-sequence RERR is stale against known route");

	EXPECT_TRUE(s_node_a.invalidate_count == 0U,
				"unknown-sequence RERR does not invalidate known route");

	const enp_route_entry_t *route =
		enp_route_table_lookup_const(&s_node_a.table, destination(1U, 3U));

	EXPECT_TRUE(route != NULL && route->state == ENP_ROUTE_STATE_ACTIVE,
				"known route remains active after unknown-sequence RERR");
}

static void test_unknown_route_rerr_is_ignored(void) {
	enp_rerr_processor_t processor;

	reset_node();

	EXPECT_TRUE(init_processor(&processor, &s_node_a),
				"A RERR processor initialized for absent-route test");

	enp_routing_rerr_t rerr = make_rerr(9U, 200U, ENP_ROUTE_ERROR_NO_ROUTE);

	enp_rerr_result_t result = enp_rerr_processor_handle(&processor, &rerr);

	EXPECT_TRUE(result == ENP_RERR_RESULT_IGNORED_NO_ROUTE,
				"RERR for absent route is ignored");

	EXPECT_TRUE(s_node_a.lookup_count == 1U,
				"absent-route RERR performs one lookup");

	EXPECT_TRUE(s_node_a.invalidate_count == 0U,
				"absent-route RERR does not invalidate");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "ENP v0.2 E1.3 RERR integration");

	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "RERR invalidation tests");

	test_newer_rerr_invalidates_route();

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Stale RERR tests");

	test_stale_rerr_does_not_invalidate();

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Unknown-sequence tests");

	test_unknown_rerr_sequence_against_known_route();

	ESP_LOGI(TAG, "--------------------------------------");

	ESP_LOGI(TAG, "Absent-route tests");

	test_unknown_route_rerr_is_ignored();

	ESP_LOGI(TAG, "======================================");

	if (s_failures == 0) {
		ESP_LOGI(TAG, "ALL E1.3 RERR INTEGRATION TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "%d E1.3 TEST(S) FAILED", s_failures);
	}

	ESP_LOGI(TAG, "======================================");
}