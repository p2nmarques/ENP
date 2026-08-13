#include "core/routing/enp_route_metric.h"

#include "esp_log.h"

static const char *TAG = "METRICTEST";
static int s_failures;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)

#define FAIL(name) do { \
    ESP_LOGE(TAG, "FAIL: %s", name); \
    ++s_failures; \
} while (0)

#define EXPECT_TRUE(condition, name) do { \
    if (condition) { \
        PASS(name); \
    } else { \
        FAIL(name); \
    } \
} while (0)

static void test_initialization(void)
{
    enp_route_metric_t metric = {0};

    EXPECT_TRUE(
        enp_route_metric_init(&metric, ENP_ROUTE_METRIC_HOP_COUNT),
        "metric initialization");

    EXPECT_TRUE(metric.valid, "initialized metric is valid");
    EXPECT_TRUE(
        metric.type == ENP_ROUTE_METRIC_HOP_COUNT,
        "metric type is HOP_COUNT");
    EXPECT_TRUE(metric.value == 0U, "initial value is zero");

    EXPECT_TRUE(
        !enp_route_metric_init(NULL, ENP_ROUTE_METRIC_HOP_COUNT),
        "initialization rejects NULL");

    EXPECT_TRUE(
        !enp_route_metric_init(
            &metric,
            (enp_route_metric_type_t)0xFFFFU),
        "unsupported metric type rejected");

    EXPECT_TRUE(
        !metric.valid,
        "unsupported metric remains invalid");
}

static void test_hop_addition(void)
{
    enp_route_metric_t metric;

    enp_route_metric_init(&metric, ENP_ROUTE_METRIC_HOP_COUNT);

    EXPECT_TRUE(
        enp_route_metric_add_hop(&metric),
        "add first hop");

    EXPECT_TRUE(
        metric.value == 1U,
        "first hop produces metric 1");

    EXPECT_TRUE(
        enp_route_metric_add_hop(&metric),
        "add second hop");

    EXPECT_TRUE(
        metric.value == 2U,
        "second hop produces metric 2");

    for (uint32_t i = 2U; i < 100U; ++i) {
        EXPECT_TRUE(
            enp_route_metric_add_hop(&metric),
            "add hop within normal range");
    }

    EXPECT_TRUE(
        metric.value == 100U,
        "100-hop sequence produces metric 100");
}

static void test_comparison(void)
{
    enp_route_metric_t zero;
    enp_route_metric_t one;
    enp_route_metric_t two;
    enp_route_metric_t equal;

    enp_route_metric_init(&zero, ENP_ROUTE_METRIC_HOP_COUNT);
    enp_route_metric_init(&one, ENP_ROUTE_METRIC_HOP_COUNT);
    enp_route_metric_init(&two, ENP_ROUTE_METRIC_HOP_COUNT);
    enp_route_metric_init(&equal, ENP_ROUTE_METRIC_HOP_COUNT);

    enp_route_metric_add_hop(&one);
    enp_route_metric_add_hop(&two);
    enp_route_metric_add_hop(&two);
    enp_route_metric_add_hop(&equal);

    EXPECT_TRUE(
        enp_route_metric_compare(&zero, &one) < 0,
        "zero-hop route is better than one-hop route");

    EXPECT_TRUE(
        enp_route_metric_compare(&one, &two) < 0,
        "one-hop route is better than two-hop route");

    EXPECT_TRUE(
        enp_route_metric_compare(&two, &one) > 0,
        "two-hop route is worse than one-hop route");

    EXPECT_TRUE(
        enp_route_metric_compare(&one, &equal) == 0,
        "equal metrics compare equal");
}

static void test_invalid_and_null(void)
{
    enp_route_metric_t valid;
    enp_route_metric_t invalid = {
        .type = ENP_ROUTE_METRIC_HOP_COUNT,
        .value = 10U,
        .valid = false
    };

    enp_route_metric_init(&valid, ENP_ROUTE_METRIC_HOP_COUNT);

    EXPECT_TRUE(
        !enp_route_metric_add_hop(&invalid),
        "invalid metric rejects hop addition");

    EXPECT_TRUE(
        enp_route_metric_compare(&valid, &invalid) < 0,
        "valid metric is better than invalid metric");

    EXPECT_TRUE(
        enp_route_metric_compare(&invalid, &valid) > 0,
        "invalid metric is worse than valid metric");

    EXPECT_TRUE(
        enp_route_metric_compare(&invalid, &invalid) == 0,
        "two invalid metrics compare equal");

    EXPECT_TRUE(
        !enp_route_metric_add_hop(NULL),
        "hop addition rejects NULL");

    EXPECT_TRUE(
        enp_route_metric_compare(NULL, &valid) > 0,
        "NULL lhs is worse than valid metric");

    EXPECT_TRUE(
        enp_route_metric_compare(&valid, NULL) < 0,
        "valid metric is better than NULL rhs");

    EXPECT_TRUE(
        enp_route_metric_compare(NULL, NULL) == 0,
        "two NULL metrics compare equal");
}

static void test_overflow(void)
{
    enp_route_metric_t metric;

    enp_route_metric_init(&metric, ENP_ROUTE_METRIC_HOP_COUNT);
    metric.value = ENP_ROUTE_METRIC_MAX_VALUE;

    EXPECT_TRUE(
        !enp_route_metric_add_hop(&metric),
        "maximum metric rejects overflow");

    EXPECT_TRUE(
        metric.value == ENP_ROUTE_METRIC_MAX_VALUE,
        "overflow does not change metric");
}

static void test_incompatible_types(void)
{
    enp_route_metric_t hop;
    enp_route_metric_t incompatible = {
        .type = (enp_route_metric_type_t)2U,
        .value = 1U,
        .valid = true
    };

    enp_route_metric_init(&hop, ENP_ROUTE_METRIC_HOP_COUNT);

    EXPECT_TRUE(
        enp_route_metric_compare(&hop, &incompatible) == 0,
        "incompatible metric types are not ranked");

    EXPECT_TRUE(
        enp_route_metric_compare(&incompatible, &hop) == 0,
        "reverse incompatible comparison is not ranked");
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "ENP v0.2 route metric test");
    ESP_LOGI(TAG, "Metric policy: HOP_COUNT");
    ESP_LOGI(TAG, "Maximum value: %u",
             (unsigned)ENP_ROUTE_METRIC_MAX_VALUE);
    ESP_LOGI(TAG, "======================================");

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Initialization tests");
    test_initialization();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Hop addition tests");
    test_hop_addition();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Comparison tests");
    test_comparison();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Invalid / NULL tests");
    test_invalid_and_null();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Overflow tests");
    test_overflow();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Metric compatibility tests");
    test_incompatible_types();

    ESP_LOGI(TAG, "======================================");

    if (s_failures == 0) {
        ESP_LOGI(TAG, "ALL ROUTE METRIC TESTS PASSED");
    } else {
        ESP_LOGE(
            TAG,
            "%d ROUTE METRIC TEST(S) FAILED",
            s_failures);
    }

    ESP_LOGI(TAG, "======================================");
}
