#include "enp_rerr_processor.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "RERRTEST";
static int s_failures;

typedef struct {
    bool lookup_ok;
    bool invalidate_ok;
    enp_rerr_route_info_t route;

    uint32_t lookup_calls;
    uint32_t invalidate_calls;

    enp_rerr_destination_t last_destination;
} mock_context_t;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)
#define FAIL(name) do { ESP_LOGE(TAG, "FAIL: %s", name); ++s_failures; } while (0)
#define EXPECT_TRUE(condition, name) do { if (condition) PASS(name); else FAIL(name); } while (0)

static bool mock_lookup(
    void *context,
    enp_rerr_destination_t destination,
    enp_rerr_route_info_t *route_info)
{
    mock_context_t *mock = context;

    ++mock->lookup_calls;
    mock->last_destination = destination;

    if (!mock->lookup_ok || route_info == NULL) {
        return false;
    }

    *route_info = mock->route;
    return true;
}

static bool mock_invalidate(
    void *context,
    enp_rerr_destination_t destination)
{
    mock_context_t *mock = context;

    ++mock->invalidate_calls;
    mock->last_destination = destination;

    return mock->invalidate_ok;
}

static void setup(
    enp_rerr_processor_t *processor,
    mock_context_t *mock)
{
    enp_rerr_processor_callbacks_t callbacks = {
        .context = mock,
        .lookup_route = mock_lookup,
        .invalidate_route = mock_invalidate
    };

    enp_rerr_processor_init(processor, &callbacks);
}

static enp_routing_rerr_t make_rerr(
    uint16_t network_id,
    uint16_t node_id,
    uint32_t sequence,
    uint8_t reason)
{
    return (enp_routing_rerr_t){
        .subtype = ENP_ROUTING_SUBTYPE_RERR,
        .flags = 0U,
        .version = ENP_ROUTING_PAYLOAD_VERSION,
        .reserved = 0U,
        .unreachable_network_id = network_id,
        .unreachable_node_id = node_id,
        .destination_sequence = sequence,
        .reason = reason,
        .reason_reserved = 0U
    };
}

static void test_initialization(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    enp_rerr_processor_callbacks_t callbacks = {
        .context = &mock,
        .lookup_route = mock_lookup,
        .invalidate_route = mock_invalidate
    };

    EXPECT_TRUE(
        enp_rerr_processor_init(&processor, &callbacks),
        "processor initialization");

    EXPECT_TRUE(
        !enp_rerr_processor_init(NULL, &callbacks),
        "initialization rejects NULL");

    EXPECT_TRUE(
        !enp_rerr_processor_init(&processor, NULL),
        "NULL callbacks rejected");
}

static void test_invalidate_equal_sequence(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &mock);

    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 10U, ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_INVALIDATED,
        "equal sequence invalidates route");

    EXPECT_TRUE(
        mock.lookup_calls == 1U,
        "route lookup performed once");

    EXPECT_TRUE(
        mock.invalidate_calls == 1U,
        "route invalidation performed once");

    EXPECT_TRUE(
        mock.last_destination.node_id == 9U,
        "correct unreachable destination used");
}

static void test_invalidate_newer_sequence(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &mock);

    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 11U, ENP_RERR_REASON_ROUTE_EXPIRED);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_INVALIDATED,
        "newer sequence invalidates route");
}

static void test_ignore_older_sequence(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &mock);

    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 9U, ENP_RERR_REASON_TRANSPORT_FAILURE);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_IGNORED_STALE,
        "older sequence does not invalidate route");

    EXPECT_TRUE(
        mock.invalidate_calls == 0U,
        "older RERR does not call invalidation");
}

static void test_unknown_sequence_rules(void)
{
    enp_rerr_processor_t processor;
    enp_routing_rerr_t rerr;

    mock_context_t known_route = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &known_route);

    rerr = make_rerr(
        1U, 9U, 0U, ENP_RERR_REASON_POLICY_INVALIDATED);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_IGNORED_STALE,
        "unknown RERR sequence cannot invalidate known route");

    mock_context_t unknown_route = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 0U}
    };

    setup(&processor, &unknown_route);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_INVALIDATED,
        "known or unknown RERR may invalidate unknown route");
}

static void test_no_route_and_inactive(void)
{
    enp_rerr_processor_t processor;
    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 10U, ENP_RERR_REASON_ROUTE_EXPIRED);

    mock_context_t no_route = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {false, false, 0U}
    };

    setup(&processor, &no_route);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_IGNORED_NO_ROUTE,
        "RERR for absent route is ignored");

    mock_context_t inactive = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, false, 10U}
    };

    setup(&processor, &inactive);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_IGNORED_INACTIVE,
        "RERR for inactive route is ignored");
}

static void test_invalid_inputs(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &mock);

    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 10U, ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE);

    rerr.subtype = 2U;
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "invalid subtype rejected");

    rerr = make_rerr(1U, 9U, 10U, ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE);
    rerr.version = 2U;
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "invalid version rejected");

    rerr = make_rerr(1U, 9U, 10U, 0U);
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "invalid reason rejected");

    rerr = make_rerr(1U, 9U, 10U, 6U);
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "reserved reason rejected");

    rerr = make_rerr(0U, 9U, 10U, ENP_RERR_REASON_ROUTE_EXPIRED);
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "zero network rejected");

    rerr = make_rerr(1U, 0U, 10U, ENP_RERR_REASON_ROUTE_EXPIRED);
    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "zero node rejected");

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, NULL) ==
            ENP_RERR_RESULT_REJECT,
        "NULL RERR rejected");
}

static void test_lookup_and_invalidate_failure(void)
{
    enp_rerr_processor_t processor;
    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 10U, ENP_RERR_REASON_TRANSPORT_FAILURE);

    mock_context_t lookup_failure = {
        .lookup_ok = false,
        .invalidate_ok = true,
        .route = {true, true, 10U}
    };

    setup(&processor, &lookup_failure);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "route lookup failure rejects RERR");

    mock_context_t invalidate_failure = {
        .lookup_ok = true,
        .invalidate_ok = false,
        .route = {true, true, 10U}
    };

    setup(&processor, &invalidate_failure);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_REJECT,
        "route invalidation failure rejects RERR");
}

static void test_sequence_wrap(void)
{
    enp_rerr_processor_t processor;
    mock_context_t mock = {
        .lookup_ok = true,
        .invalidate_ok = true,
        .route = {true, true, 0xFFFFFFFEU}
    };

    setup(&processor, &mock);

    enp_routing_rerr_t rerr =
        make_rerr(1U, 9U, 1U, ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE);

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_INVALIDATED,
        "newer RERR across sequence wrap invalidates route");

    mock.route.destination_sequence = 2U;
    setup(&processor, &mock);

    rerr.destination_sequence = 0xFFFFFFFEU;

    EXPECT_TRUE(
        enp_rerr_processor_handle(&processor, &rerr) ==
            ENP_RERR_RESULT_IGNORED_STALE,
        "older RERR across sequence wrap is ignored");
}

static void test_all_reasons(void)
{
    for (uint8_t reason = ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE;
         reason <= ENP_RERR_REASON_POLICY_INVALIDATED;
         ++reason) {
        enp_rerr_processor_t processor;
        mock_context_t mock = {
            .lookup_ok = true,
            .invalidate_ok = true,
            .route = {true, true, 10U}
        };

        setup(&processor, &mock);

        enp_routing_rerr_t rerr = make_rerr(1U, 9U, 10U, reason);

        EXPECT_TRUE(
            enp_rerr_processor_handle(&processor, &rerr) ==
                ENP_RERR_RESULT_INVALIDATED,
            "valid RERR reason accepted");
    }
}

static void test_wire_round_trip(void)
{
    enp_routing_rerr_t input =
        make_rerr(1U, 9U, 0x12345678U, ENP_RERR_REASON_LOCAL_REPAIR_FAILED);

    uint8_t buffer[ENP_ROUTING_RERR_WIRE_SIZE];
    enp_routing_rerr_t output;

    EXPECT_TRUE(
        enp_routing_rerr_encode(&input, buffer, sizeof(buffer)),
        "RERR encode");

    EXPECT_TRUE(
        enp_routing_rerr_decode(&output, buffer, sizeof(buffer)),
        "RERR decode");

    EXPECT_TRUE(
        memcmp(&input, &output, sizeof(input)) == 0,
        "RERR round-trip");
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "ENP v0.2 R4-D RERR processor test");
    ESP_LOGI(TAG, "RERR wire size: %u", (unsigned)ENP_ROUTING_RERR_WIRE_SIZE);
    ESP_LOGI(TAG, "======================================");

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Initialization tests");
    test_initialization();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Equal/newer sequence tests");
    test_invalidate_equal_sequence();
    test_invalidate_newer_sequence();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Stale sequence tests");
    test_ignore_older_sequence();
    test_unknown_sequence_rules();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Route applicability tests");
    test_no_route_and_inactive();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Invalid-input tests");
    test_invalid_inputs();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Callback failure tests");
    test_lookup_and_invalidate_failure();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Sequence wrap tests");
    test_sequence_wrap();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Reason-code tests");
    test_all_reasons();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Wire integration tests");
    test_wire_round_trip();

    ESP_LOGI(TAG, "======================================");

    if (s_failures == 0) {
        ESP_LOGI(TAG, "ALL RERR PROCESSOR TESTS PASSED");
    } else {
        ESP_LOGE(TAG, "%d RERR PROCESSOR TEST(S) FAILED", s_failures);
    }

    ESP_LOGI(TAG, "======================================");
}
