#include "core/routing/enp_route_table.h"

#include "esp_log.h"

static const char *TAG = "ROUTETABLE";
static int s_failures;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)

#define FAIL(name)                                                             \
	do {                                                                       \
		ESP_LOGE(TAG, "FAIL: %s", name);                                       \
		++s_failures;                                                          \
	} while (0)

#define EXPECT_TRUE(condition, name)                                           \
	do {                                                                       \
		if (condition)                                                         \
			PASS(name);                                                        \
		else                                                                   \
			FAIL(name);                                                        \
	} while (0)

static enp_route_entry_t make_entry(uint16_t destination, uint16_t next_hop,
									uint16_t metric, uint32_t sequence,
									uint32_t expiry) {
	enp_route_entry_t entry = {0};

	entry.destination.network_id = 1U;
	entry.destination.node_id = destination;

	entry.next_hop.network_id = 1U;
	entry.next_hop.node_id = next_hop;

	enp_route_metric_init(&entry.metric, ENP_ROUTE_METRIC_HOP_COUNT);

	entry.metric.value = metric;

	entry.route_sequence = sequence;
	entry.expires_at_ms = expiry;
	entry.state = ENP_ROUTE_STATE_ACTIVE;

	return entry;
}

static void test_initialization(void) {
	enp_route_table_t table;

	EXPECT_TRUE(enp_route_table_init(&table), "table initialization");

	EXPECT_TRUE(enp_route_table_count(&table) == 0U,
				"initial table count is zero");

	EXPECT_TRUE(enp_route_table_active_count(&table) == 0U,
				"initial active count is zero");

	EXPECT_TRUE(enp_route_table_init(NULL) == false,
				"initialization rejects NULL");
}

static void test_insert_lookup(void) {
	enp_route_table_t table;
	enp_route_entry_t entry;
	enp_route_entry_t *found;

	enp_route_table_init(&table);

	entry = make_entry(2U, 10U, 1U, 100U, 10000U);

	EXPECT_TRUE(enp_route_table_insert(&table, &entry), "insert route");

	EXPECT_TRUE(enp_route_table_count(&table) == 1U, "count after insert");

	EXPECT_TRUE(enp_route_table_active_count(&table) == 1U,
				"active count after insert");

	found = enp_route_table_lookup(&table, entry.destination);

	EXPECT_TRUE(found != NULL, "lookup finds active route");

	EXPECT_TRUE(found != NULL && found->next_hop.node_id == 10U,
				"lookup returns correct next hop");

	EXPECT_TRUE(found != NULL && found->metric.value == 1U,
				"lookup returns correct metric");

	EXPECT_TRUE(enp_route_table_lookup(
					&table, (enp_route_destination_t){1U, 99U}) == NULL,
				"lookup misses unknown destination");

	EXPECT_TRUE(enp_route_table_insert(&table, &entry) == false,
				"duplicate destination rejected");
}

static void test_update(void) {
	enp_route_table_t table;
	enp_route_entry_t entry;
	enp_route_entry_t updated;
	enp_route_entry_t *found;

	enp_route_table_init(&table);

	entry = make_entry(3U, 11U, 4U, 10U, 1000U);
	updated = make_entry(3U, 12U, 2U, 11U, 2000U);

	EXPECT_TRUE(enp_route_table_insert(&table, &entry),
				"insert route for update");

	EXPECT_TRUE(enp_route_table_update(&table, &updated),
				"update existing route");

	found = enp_route_table_lookup(&table, updated.destination);

	EXPECT_TRUE(found != NULL && found->next_hop.node_id == 12U,
				"update changes next hop");

	EXPECT_TRUE(found != NULL && found->metric.value == 2U,
				"update changes metric");

	EXPECT_TRUE(found != NULL && found->route_sequence == 11U,
				"update changes sequence");

	{
		enp_route_entry_t unknown = make_entry(99U, 199U, 3U, 12U, 3000U);

		EXPECT_TRUE(enp_route_table_update(&table, &unknown) == false,
					"update unknown destination rejected");
	}
}

static void test_invalidation(void) {
	enp_route_table_t table;
	enp_route_entry_t entry;

	enp_route_table_init(&table);
	entry = make_entry(4U, 14U, 2U, 20U, 5000U);

	enp_route_table_insert(&table, &entry);

	EXPECT_TRUE(enp_route_table_invalidate(&table, entry.destination),
				"invalidate route");

	EXPECT_TRUE(enp_route_table_lookup(&table, entry.destination) == NULL,
				"invalidated route is not returned by lookup");

	EXPECT_TRUE(enp_route_table_count(&table) == 1U,
				"invalidated route remains in table");

	EXPECT_TRUE(enp_route_table_active_count(&table) == 0U,
				"invalidated route is not active");
}

static void test_remove(void) {
	enp_route_table_t table;
	enp_route_entry_t a;
	enp_route_entry_t b;

	enp_route_table_init(&table);

	a = make_entry(5U, 15U, 1U, 30U, 5000U);
	b = make_entry(6U, 16U, 2U, 31U, 5000U);

	enp_route_table_insert(&table, &a);
	enp_route_table_insert(&table, &b);

	EXPECT_TRUE(enp_route_table_remove(&table, a.destination), "remove route");

	EXPECT_TRUE(enp_route_table_count(&table) == 1U, "count after removal");

	EXPECT_TRUE(enp_route_table_lookup(&table, a.destination) == NULL,
				"removed route is absent");

	EXPECT_TRUE(enp_route_table_lookup(&table, b.destination) != NULL,
				"remaining route survives removal");

	EXPECT_TRUE(!enp_route_table_remove(&table, a.destination),
				"removing unknown route rejected");
}

static void test_expiration(void) {
	enp_route_table_t table;
	enp_route_entry_t a;
	enp_route_entry_t b;

	enp_route_table_init(&table);

	a = make_entry(7U, 17U, 1U, 40U, 1000U);
	b = make_entry(8U, 18U, 2U, 41U, 2000U);

	enp_route_table_insert(&table, &a);
	enp_route_table_insert(&table, &b);

	EXPECT_TRUE(enp_route_table_expire(&table, 999U) == 0U,
				"no route expires before deadline");

	EXPECT_TRUE(enp_route_table_expire(&table, 1000U) == 1U,
				"route expires at deadline");

	EXPECT_TRUE(enp_route_table_active_count(&table) == 1U,
				"one active route remains");

	EXPECT_TRUE(enp_route_table_lookup(&table, a.destination) == NULL,
				"expired route is not active");

	EXPECT_TRUE(enp_route_table_expire(&table, 2000U) == 1U,
				"second route expires at deadline");

	EXPECT_TRUE(enp_route_table_active_count(&table) == 0U,
				"no active routes remain");
}

static void test_expiration_wrap(void) {
	enp_route_table_t table;
	enp_route_entry_t entry;

	enp_route_table_init(&table);

	entry = make_entry(9U, 19U, 1U, 50U, 5U);

	enp_route_table_insert(&table, &entry);

	EXPECT_TRUE(enp_route_table_expire(&table, UINT32_MAX - 2U) == 0U,
				"route remains active before wrap deadline");

	EXPECT_TRUE(enp_route_table_expire(&table, 4U) == 0U,
				"route remains active across clock wrap");

	EXPECT_TRUE(enp_route_table_expire(&table, 5U) == 1U,
				"route expires correctly across clock wrap");
}

static void test_capacity(void) {
	enp_route_table_t table;

	enp_route_table_init(&table);

	for (uint16_t i = 0U; i < ENP_MAX_ROUTES; ++i) {
		enp_route_entry_t entry =
			make_entry((uint16_t)(100U + i), (uint16_t)(200U + i), 1U,
					   (uint32_t)i, 100000U);

		EXPECT_TRUE(enp_route_table_insert(&table, &entry),
					"capacity insertion");
	}

	EXPECT_TRUE(enp_route_table_count(&table) == ENP_MAX_ROUTES,
				"table reaches ENP_MAX_ROUTES");

	{
		enp_route_entry_t extra = make_entry(999U, 1999U, 1U, 999U, 100000U);

		EXPECT_TRUE(!enp_route_table_insert(&table, &extra),
					"full table rejects additional route");
	}
}

static void test_invalid_entries(void) {
	enp_route_table_t table;
	enp_route_entry_t entry;

	enp_route_table_init(&table);

	entry = make_entry(0U, 1U, 1U, 1U, 100U);
	entry.destination.network_id = 0U;
	entry.destination.node_id = 0U;

	EXPECT_TRUE(!enp_route_table_insert(&table, &entry),
				"zero destination rejected");

	entry = make_entry(10U, 20U, 1U, 1U, 100U);
	entry.metric.valid = false;

	EXPECT_TRUE(!enp_route_table_insert(&table, &entry),
				"invalid metric rejected");

	entry = make_entry(11U, 21U, 1U, 1U, 100U);
	entry.state = ENP_ROUTE_STATE_INVALID;

	EXPECT_TRUE(!enp_route_table_insert(&table, &entry),
				"invalid state rejected");

	EXPECT_TRUE(!enp_route_table_insert(&table, NULL), "NULL entry rejected");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "ENP v0.2 route table test");
	ESP_LOGI(TAG, "Maximum routes: %u", (unsigned)ENP_MAX_ROUTES);
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Initialization tests");
	test_initialization();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Insert / lookup tests");
	test_insert_lookup();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Update tests");
	test_update();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Invalidation tests");
	test_invalidation();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Removal tests");
	test_remove();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Expiration tests");
	test_expiration();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Clock-wrap expiration test");
	test_expiration_wrap();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Capacity tests");
	test_capacity();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Invalid-entry tests");
	test_invalid_entries();

	ESP_LOGI(TAG, "======================================");

	if (s_failures == 0) {
		ESP_LOGI(TAG, "ALL ROUTE TABLE TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "%d ROUTE TABLE TEST(S) FAILED", s_failures);
	}

	ESP_LOGI(TAG, "======================================");
}
