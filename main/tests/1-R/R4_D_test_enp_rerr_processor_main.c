#include "core/routing/enp_rerr_processor.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "RERRTEST";
static int s_failures;

#define EXPECT_TRUE(expr, text)                                                \
	do {                                                                       \
		if (expr) {                                                            \
			ESP_LOGI(TAG, "PASS: %s", text);                                   \
		} else {                                                               \
			ESP_LOGE(TAG, "FAIL: %s", text);                                   \
			s_failures++;                                                      \
		}                                                                      \
	} while (0)

typedef struct {
	bool lookup_ok;
	bool invalidate_ok;
	bool installed;
	bool active;
	uint32_t stored_sequence;
	unsigned lookup_calls;
	unsigned invalidate_calls;
	enp_rerr_destination_t last_destination;
} mock_context_t;

static bool mock_lookup(void *context, enp_rerr_destination_t destination,
						enp_rerr_route_info_t *route_info) {
	mock_context_t *m = context;
	m->lookup_calls++;
	m->last_destination = destination;
	if (!m->lookup_ok || route_info == NULL)
		return false;
	route_info->installed = m->installed;
	route_info->active = m->active;
	route_info->destination_sequence = m->stored_sequence;
	return true;
}

static bool mock_invalidate(void *context, enp_rerr_destination_t destination) {
	mock_context_t *m = context;
	m->invalidate_calls++;
	m->last_destination = destination;
	return m->invalidate_ok;
}

static void setup(enp_rerr_processor_t *processor, mock_context_t *mock) {
	enp_rerr_processor_callbacks_t cb = {
		.context = mock,
		.lookup_route = mock_lookup,
		.invalidate_route = mock_invalidate,
	};
	EXPECT_TRUE(enp_rerr_processor_init(processor, &cb),
				"processor initialization");
}

static enp_routing_rerr_t make_rerr(uint16_t node, uint32_t sequence,
									uint8_t reason) {
	return (enp_routing_rerr_t){
		.payload_version = ENP_ROUTING_PAYLOAD_VERSION,
		.subtype = ENP_ROUTING_SUBTYPE_RERR,
		.unreachable_network_id = 1U,
		.unreachable_node_id = node,
		.destination_sequence = sequence,
		.reason = reason,
		.reserved_0 = 0U,
		.reserved_1 = 0U,
	};
}

static void test_init_invalid(void) {
	enp_rerr_processor_t p;
	enp_rerr_processor_callbacks_t cb = {0};
	EXPECT_TRUE(!enp_rerr_processor_init(NULL, &cb),
				"initialization rejects NULL");
	EXPECT_TRUE(!enp_rerr_processor_init(&p, NULL), "NULL callbacks rejected");
}

static void test_equal_newer(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = true,
						.active = true,
						.stored_sequence = 10U};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 10U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_INVALIDATED,
				"equal sequence invalidates route");
	EXPECT_TRUE(m.lookup_calls == 1U, "route lookup performed once");
	EXPECT_TRUE(m.invalidate_calls == 1U, "route invalidation performed once");
	EXPECT_TRUE(m.last_destination.node_id == 7U,
				"correct unreachable destination used");

	m.invalidate_calls = 0;
	m.lookup_calls = 0;
	m.stored_sequence = 10U;
	r = make_rerr(7U, 11U, ENP_ROUTE_ERROR_ROUTE_EXPIRED);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_INVALIDATED,
				"newer sequence invalidates route");
}

static void test_stale(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = true,
						.active = true,
						.stored_sequence = 10U};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 9U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_IGNORED_STALE,
				"older sequence does not invalidate route");
	EXPECT_TRUE(m.invalidate_calls == 0U,
				"older RERR does not call invalidation");
	r = make_rerr(7U, 0U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_IGNORED_STALE,
				"unknown RERR sequence cannot invalidate known route");

	m.stored_sequence = 0U;
	m.invalidate_calls = 0;
	r = make_rerr(8U, 0U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_INVALIDATED,
				"known or unknown RERR may invalidate unknown route");
}

static void test_applicability(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = false,
						.active = false,
						.stored_sequence = 0U};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_IGNORED_NO_ROUTE,
				"RERR for absent route is ignored");
	m.installed = true;
	m.active = false;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_IGNORED_INACTIVE,
				"RERR for inactive route is ignored");
}

static void test_invalid(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = true,
						.active = true};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	r.subtype = ENP_ROUTING_SUBTYPE_RREQ;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"invalid subtype rejected");
	r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	r.payload_version = 0xFFU;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"invalid version rejected");
	r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_UNKNOWN);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"invalid reason rejected");
	r = make_rerr(7U, 1U, 6U);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"reserved reason rejected");
	r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	r.unreachable_network_id = 0U;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"zero network rejected");
	r = make_rerr(0U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"zero node rejected");
	r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	r.reserved_0 = 1U;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"non-zero reserved_0 rejected");
	r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	r.reserved_1 = 1U;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"non-zero reserved_1 rejected");
	EXPECT_TRUE(enp_rerr_processor_handle(&p, NULL) == ENP_RERR_RESULT_REJECT,
				"NULL RERR rejected");
}

static void test_callback_failures(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = false,
						.invalidate_ok = true,
						.installed = true,
						.active = true};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"route lookup failure rejects RERR");
	m.lookup_ok = true;
	m.invalidate_ok = false;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) == ENP_RERR_RESULT_REJECT,
				"route invalidation failure rejects RERR");
}

static void test_wrap(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = true,
						.active = true,
						.stored_sequence = 0xFFFFFFFEU};
	setup(&p, &m);
	enp_routing_rerr_t r = make_rerr(7U, 1U, ENP_ROUTE_ERROR_NO_ROUTE);
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_INVALIDATED,
				"newer RERR across sequence wrap invalidates route");
	m.invalidate_calls = 0;
	m.stored_sequence = 1U;
	r.destination_sequence = 0xFFFFFFFEU;
	EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
					ENP_RERR_RESULT_IGNORED_STALE,
				"older RERR across sequence wrap is ignored");
}

static void test_reasons(void) {
	enp_rerr_processor_t p;
	mock_context_t m = {.lookup_ok = true,
						.invalidate_ok = true,
						.installed = true,
						.active = true,
						.stored_sequence = 1U};
	setup(&p, &m);
	for (uint8_t reason = ENP_ROUTE_ERROR_NO_ROUTE;
		 reason <= ENP_ROUTE_ERROR_TTL_EXPIRED; ++reason) {
		enp_routing_rerr_t r = make_rerr((uint16_t)(10U + reason), 1U, reason);
		m.invalidate_calls = 0;
		EXPECT_TRUE(enp_rerr_processor_handle(&p, &r) ==
						ENP_RERR_RESULT_INVALIDATED,
					"valid RERR reason accepted");
	}
}

static void test_wire(void) {
	enp_routing_rerr_t in =
		make_rerr(7U, 0x12345678U, ENP_ROUTE_ERROR_TTL_EXPIRED);
	uint8_t buffer[ENP_ROUTING_RERR_WIRE_SIZE];
	enp_routing_rerr_t out;
	EXPECT_TRUE(enp_routing_rerr_encode(&in, buffer, sizeof(buffer)),
				"RERR encode");
	EXPECT_TRUE(enp_routing_rerr_decode(&out, buffer, sizeof(buffer)),
				"RERR decode");
	EXPECT_TRUE(memcmp(&in, &out, sizeof(in)) == 0, "RERR round-trip");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "ENP v0.2 R4-D RERR processor test");
	ESP_LOGI(TAG, "RERR wire size: %u", (unsigned)ENP_ROUTING_RERR_WIRE_SIZE);
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Initialization tests");
	test_init_invalid();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Equal/newer sequence tests");
	test_equal_newer();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Stale sequence tests");
	test_stale();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Route applicability tests");
	test_applicability();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Invalid-input tests");
	test_invalid();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Callback failure tests");
	test_callback_failures();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Sequence wrap tests");
	test_wrap();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Reason-code tests");
	test_reasons();
	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Wire integration tests");
	test_wire();
	ESP_LOGI(TAG, "======================================");
	if (s_failures == 0)
		ESP_LOGI(TAG, "ALL RERR PROCESSOR TESTS PASSED");
	else
		ESP_LOGE(TAG, "%d RERR PROCESSOR TEST(S) FAILED", s_failures);
	ESP_LOGI(TAG, "======================================");
}
