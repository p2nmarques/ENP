#include "core/routing/enp_route_discovery.h"

#include "esp_log.h"

static const char *TAG = "DISCOVTEST";
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

static const enp_discovery_destination_t DEST = {.network_id = 1U,
												 .node_id = 2U};

static const enp_discovery_destination_t OTHER_DEST = {.network_id = 1U,
													   .node_id = 3U};

static void test_initialization(void) {
	enp_route_discovery_t discovery;

	EXPECT_TRUE(enp_route_discovery_init(&discovery),
				"discovery initialization");

	EXPECT_TRUE(enp_route_discovery_state(&discovery) ==
					ENP_DISCOVERY_STATE_IDLE,
				"initial state is IDLE");

	EXPECT_TRUE(!enp_route_discovery_is_active(&discovery),
				"initialized discovery is inactive");

	EXPECT_TRUE(!enp_route_discovery_init(NULL), "initialization rejects NULL");
}

static void test_start(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	EXPECT_TRUE(
		enp_route_discovery_start(&discovery, DEST, 0x1001U, 55U, 8U, 1000U),
		"start discovery");

	EXPECT_TRUE(enp_route_discovery_state(&discovery) ==
					ENP_DISCOVERY_STATE_REQUESTING,
				"start enters REQUESTING");

	EXPECT_TRUE(enp_route_discovery_is_active(&discovery),
				"started discovery is active");

	EXPECT_TRUE(discovery.route_request_id == 0x1001U, "request ID stored");

	EXPECT_TRUE(discovery.destination_sequence == 55U,
				"destination sequence stored");

	EXPECT_TRUE(discovery.ttl == 8U, "TTL stored");

	EXPECT_TRUE(discovery.retry_count == 0U, "initial retry count is zero");

	EXPECT_TRUE(discovery.deadline_ms == 1000U + ENP_DISCOVERY_TIMEOUT_MS,
				"initial deadline calculated");

	EXPECT_TRUE(
		!enp_route_discovery_start(&discovery, DEST, 0x1002U, 56U, 8U, 1000U),
		"active discovery rejects second start");
}

static void test_start_validation(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	EXPECT_TRUE(
		!enp_route_discovery_start(
			&discovery, (enp_discovery_destination_t){0U, 0U}, 1U, 1U, 8U, 0U),
		"zero destination rejected");

	EXPECT_TRUE(!enp_route_discovery_start(&discovery, DEST, 0U, 1U, 8U, 0U),
				"zero request ID rejected");

	EXPECT_TRUE(!enp_route_discovery_start(&discovery, DEST, 1U, 1U, 0U, 0U),
				"zero TTL rejected");

	EXPECT_TRUE(!enp_route_discovery_start(NULL, DEST, 1U, 1U, 8U, 0U),
				"start rejects NULL");
}

static void test_rrep_completion(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	enp_route_discovery_start(&discovery, DEST, 0x2001U, 77U, 8U, 1000U);

	EXPECT_TRUE(!enp_route_discovery_on_rrep(&discovery, OTHER_DEST, 0x2001U),
				"RREP for wrong destination rejected");

	EXPECT_TRUE(enp_route_discovery_is_active(&discovery),
				"wrong destination does not complete discovery");

	EXPECT_TRUE(!enp_route_discovery_on_rrep(&discovery, DEST, 76U),
				"RREP with older destination sequence rejected");

	EXPECT_TRUE(enp_route_discovery_is_active(&discovery),
				"older destination sequence does not complete discovery");

	EXPECT_TRUE(enp_route_discovery_on_rrep(&discovery, DEST, 77U),
				"RREP with matching destination sequence accepted");

	EXPECT_TRUE(enp_route_discovery_state(&discovery) ==
					ENP_DISCOVERY_STATE_COMPLETE,
				"matching RREP enters COMPLETE");

	EXPECT_TRUE(!enp_route_discovery_is_active(&discovery),
				"completed discovery is inactive");

	EXPECT_TRUE(!enp_route_discovery_on_rrep(&discovery, DEST, 77U),
				"RREP after completion rejected");
}

static void test_timeout_and_retry(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	enp_route_discovery_start(&discovery, DEST, 0x3001U, 1U, 8U, 1000U);

	EXPECT_TRUE(!enp_route_discovery_on_timeout(
					&discovery, 1000U + ENP_DISCOVERY_TIMEOUT_MS - 1U),
				"timeout before deadline does not retry");

	EXPECT_TRUE(enp_route_discovery_on_timeout(
					&discovery, 1000U + ENP_DISCOVERY_TIMEOUT_MS),
				"timeout at deadline starts retry");

	EXPECT_TRUE(discovery.retry_count == 1U,
				"first retry increments retry count");

	EXPECT_TRUE(discovery.state == ENP_DISCOVERY_STATE_REQUESTING,
				"retry remains REQUESTING");

	EXPECT_TRUE(enp_route_discovery_on_timeout(
					&discovery, 1000U + 2U * ENP_DISCOVERY_TIMEOUT_MS),
				"second timeout starts retry");

	EXPECT_TRUE(discovery.retry_count == 2U,
				"second retry increments retry count");

	EXPECT_TRUE(enp_route_discovery_on_timeout(
					&discovery, 1000U + 3U * ENP_DISCOVERY_TIMEOUT_MS),
				"third timeout starts retry");

	EXPECT_TRUE(discovery.retry_count == 3U,
				"third retry increments retry count");
}

static void test_failure(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	enp_route_discovery_start(&discovery, DEST, 0x4001U, 1U, 8U, 0U);

	/*
	 * ENP_MAX_RETRIES is the number of retries after the initial request.
	 * Therefore one additional timeout after the final retry causes failure.
	 */
	for (uint32_t i = 0U; i < ENP_MAX_RETRIES; ++i) {
		uint32_t now = ENP_DISCOVERY_TIMEOUT_MS * (i + 1U);

		EXPECT_TRUE(enp_route_discovery_on_timeout(&discovery, now),
					"timeout schedules retry");
	}

	EXPECT_TRUE(discovery.retry_count == ENP_MAX_RETRIES,
				"retry count reaches ENP_MAX_RETRIES");

	EXPECT_TRUE(
		!enp_route_discovery_on_timeout(&discovery, ENP_DISCOVERY_TIMEOUT_MS *
														(ENP_MAX_RETRIES + 1U)),
		"final timeout does not schedule retry");

	EXPECT_TRUE(enp_route_discovery_state(&discovery) ==
					ENP_DISCOVERY_STATE_FAILED,
				"final timeout enters FAILED");

	EXPECT_TRUE(!enp_route_discovery_is_active(&discovery),
				"failed discovery is inactive");
}

static void test_reuse(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	enp_route_discovery_start(&discovery, DEST, 0x5001U, 1U, 8U, 100U);

	enp_route_discovery_on_rrep(&discovery, DEST, 1U);

	EXPECT_TRUE(enp_route_discovery_start(&discovery, OTHER_DEST, 0x5002U, 2U,
										  6U, 200U),
				"completed discovery can be reused");

	EXPECT_TRUE(discovery.state == ENP_DISCOVERY_STATE_REQUESTING,
				"reuse enters REQUESTING");

	EXPECT_TRUE(discovery.route_request_id == 0x5002U,
				"reuse replaces request ID");

	EXPECT_TRUE(discovery.destination.node_id == OTHER_DEST.node_id,
				"reuse replaces destination");

	EXPECT_TRUE(discovery.retry_count == 0U, "reuse resets retry count");
}

static void test_clock_wrap(void) {
	enp_route_discovery_t discovery;

	enp_route_discovery_init(&discovery);

	uint32_t start = UINT32_MAX - 500U;

	enp_route_discovery_start(&discovery, DEST, 0x6001U, 1U, 8U, start);

	EXPECT_TRUE(!enp_route_discovery_on_timeout(
					&discovery, start + ENP_DISCOVERY_TIMEOUT_MS - 1U),
				"discovery remains active across clock wrap");

	EXPECT_TRUE(enp_route_discovery_on_timeout(
					&discovery, start + ENP_DISCOVERY_TIMEOUT_MS),
				"timeout works across clock wrap");

	EXPECT_TRUE(discovery.retry_count == 1U,
				"wrap-safe timeout increments retry count");
}

void app_main(void) {
	ESP_LOGI(TAG, "======================================");
	ESP_LOGI(TAG, "ENP v0.2 route discovery state test (R4-A.1)");
	ESP_LOGI(TAG, "Timeout: %u ms", (unsigned)ENP_DISCOVERY_TIMEOUT_MS);
	ESP_LOGI(TAG, "Max retries: %u", (unsigned)ENP_MAX_RETRIES);
	ESP_LOGI(TAG, "Default TTL: %u", (unsigned)ENP_DISCOVERY_DEFAULT_TTL);
	ESP_LOGI(TAG, "======================================");

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Initialization tests");
	test_initialization();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Start tests");
	test_start();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Start validation tests");
	test_start_validation();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "RREP completion tests");
	test_rrep_completion();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Timeout / retry tests");
	test_timeout_and_retry();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Failure tests");
	test_failure();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Reuse tests");
	test_reuse();

	ESP_LOGI(TAG, "--------------------------------------");
	ESP_LOGI(TAG, "Clock-wrap tests");
	test_clock_wrap();

	ESP_LOGI(TAG, "======================================");

	if (s_failures == 0) {
		ESP_LOGI(TAG, "ALL ROUTE DISCOVERY STATE TESTS PASSED");
	} else {
		ESP_LOGE(TAG, "%d ROUTE DISCOVERY STATE TEST(S) FAILED", s_failures);
	}

	ESP_LOGI(TAG, "======================================");
}
