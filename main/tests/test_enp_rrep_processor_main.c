#include "core/routing/enp_rrep_processor.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "RREPTEST";
static int s_failures;

typedef struct {
    bool update_ok;
    bool lookup_ok;
    bool completion_ok;

    uint32_t update_calls;
    uint32_t lookup_calls;
    uint32_t completion_calls;

    enp_rrep_node_t last_destination;
    enp_rrep_node_t last_next_hop;
    enp_rrep_node_t lookup_destination;

    enp_route_sequence_t last_sequence;
    uint8_t last_hop_count;
    uint32_t last_lifetime;
    uint32_t last_metric;
} mock_context_t;

#define PASS(name) ESP_LOGI(TAG, "PASS: %s", name)

#define FAIL(name) do { \
    ESP_LOGE(TAG, "FAIL: %s", name); \
    ++s_failures; \
} while (0)

#define EXPECT_TRUE(condition, name) do { \
    if (condition) PASS(name); \
    else FAIL(name); \
} while (0)

static bool mock_update_route(
    void *context,
    enp_rrep_node_t destination,
    enp_rrep_node_t next_hop,
    enp_route_sequence_t destination_sequence,
    uint8_t hop_count,
    uint32_t lifetime_ms,
    uint32_t metric)
{
    mock_context_t *mock = context;

    ++mock->update_calls;
    mock->last_destination = destination;
    mock->last_next_hop = next_hop;
    mock->last_sequence = destination_sequence;
    mock->last_hop_count = hop_count;
    mock->last_lifetime = lifetime_ms;
    mock->last_metric = metric;

    return mock->update_ok;
}

static bool mock_lookup_next_hop(
    void *context,
    enp_rrep_node_t destination,
    enp_rrep_node_t *next_hop)
{
    mock_context_t *mock = context;

    ++mock->lookup_calls;
    mock->lookup_destination = destination;

    if (!mock->lookup_ok || next_hop == NULL) {
        return false;
    }

    *next_hop = (enp_rrep_node_t){1U, 7U};
    return true;
}

static bool mock_discovery_complete(
    void *context,
    enp_rrep_node_t destination,
    enp_route_sequence_t destination_sequence)
{
    mock_context_t *mock = context;

    ++mock->completion_calls;
    mock->last_destination = destination;
    mock->last_sequence = destination_sequence;

    return mock->completion_ok;
}

static enp_routing_rrep_t make_rrep(
    uint16_t destination_node,
    uint32_t sequence,
    uint8_t hop_count,
    uint32_t lifetime)
{
    return (enp_routing_rrep_t){
        .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
        .subtype = ENP_ROUTING_SUBTYPE_RREP,
        .destination_network_id = 1U,
        .destination_node_id = destination_node,
        .destination_sequence = sequence,
        .hop_count = hop_count,
        .reserved_0 = 0U,
        .route_lifetime_ms = lifetime,
        .reserved_1 = 0U
    };
}

static void setup(
    enp_rrep_processor_t *processor,
    mock_context_t *mock,
    uint16_t local_node,
    uint16_t originator_node)
{
    enp_rrep_processor_callbacks_t callbacks = {
        .context = mock,
        .update_route = mock_update_route,
        .lookup_next_hop = mock_lookup_next_hop,
        .discovery_complete = mock_discovery_complete
    };

    enp_rrep_processor_init(
        processor,
        (enp_rrep_node_t){1U, local_node},
        (enp_rrep_node_t){1U, originator_node},
        &callbacks);
}

static void test_initialization(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = true
    };

    enp_rrep_processor_callbacks_t callbacks = {
        .context = &mock,
        .update_route = mock_update_route,
        .lookup_next_hop = mock_lookup_next_hop,
        .discovery_complete = mock_discovery_complete
    };

    EXPECT_TRUE(
        enp_rrep_processor_init(
            &processor,
            (enp_rrep_node_t){1U, 3U},
            (enp_rrep_node_t){1U, 1U},
            &callbacks),
        "processor initialization");

    EXPECT_TRUE(
        !enp_rrep_processor_init(
            NULL,
            (enp_rrep_node_t){1U, 3U},
            (enp_rrep_node_t){1U, 1U},
            &callbacks),
        "initialization rejects NULL");

    EXPECT_TRUE(
        !enp_rrep_processor_init(
            &processor,
            (enp_rrep_node_t){0U, 0U},
            (enp_rrep_node_t){1U, 1U},
            &callbacks),
        "invalid local node rejected");

    EXPECT_TRUE(
        !enp_rrep_processor_init(
            &processor,
            (enp_rrep_node_t){1U, 3U},
            (enp_rrep_node_t){0U, 0U},
            &callbacks),
        "invalid originator rejected");

    EXPECT_TRUE(
        !enp_rrep_processor_init(
            &processor,
            (enp_rrep_node_t){1U, 3U},
            (enp_rrep_node_t){1U, 1U},
            NULL),
        "NULL callbacks rejected");
}

static void test_forward(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = true
    };

    setup(&processor, &mock, 3U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 100U, 2U, 5000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_FORWARD,
        "RREP is forwarded toward originator");

    EXPECT_TRUE(
        mock.update_calls == 1U,
        "forward route updated once");

    EXPECT_TRUE(
        mock.last_destination.node_id == 9U,
        "route update stores RREP destination");

    EXPECT_TRUE(
        mock.last_next_hop.node_id == 8U,
        "route update stores previous hop");

    EXPECT_TRUE(
        mock.last_sequence == 100U,
        "route update stores destination sequence");

    EXPECT_TRUE(
        mock.last_hop_count == 2U,
        "route update stores RREP hop count");

    EXPECT_TRUE(
        mock.last_lifetime == 5000U,
        "route update stores route lifetime");

    EXPECT_TRUE(
        mock.last_metric == 2U,
        "route update stores metric");

    EXPECT_TRUE(
        mock.lookup_calls == 1U,
        "originator next-hop lookup performed");

    EXPECT_TRUE(
        mock.lookup_destination.node_id == 1U,
        "lookup targets discovery originator");

    EXPECT_TRUE(
        output.hop_count == 3U,
        "forwarded RREP increments hop count");

    EXPECT_TRUE(
        output.destination_node_id == input.destination_node_id,
        "forwarded RREP preserves destination");
}

static void test_complete_at_originator(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = true
    };

    setup(&processor, &mock, 1U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 200U, 4U, 10000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 2U},
            &input,
            &output) == ENP_RREP_RESULT_COMPLETE,
        "RREP completes discovery at originator");

    EXPECT_TRUE(
        mock.update_calls == 1U,
        "originator updates forward route");

    EXPECT_TRUE(
        mock.completion_calls == 1U,
        "discovery completion callback invoked");

    EXPECT_TRUE(
        mock.lookup_calls == 0U,
        "originator does not perform next-hop lookup");

    EXPECT_TRUE(
        mock.last_destination.node_id == 9U,
        "completion reports route destination");

    EXPECT_TRUE(
        mock.last_sequence == 200U,
        "completion reports destination sequence");
}

static void test_invalid(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = true
    };

    setup(&processor, &mock, 3U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 1U, 2U, 5000U);
    enp_routing_rrep_t output;

    input.payload_version = 0xFFU;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "invalid version rejected");

    input = make_rrep(9U, 2U, 2U, 5000U);
    input.subtype = ENP_ROUTING_SUBTYPE_RREQ;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "invalid subtype rejected");

    input = make_rrep(0U, 3U, 2U, 5000U);

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "zero destination rejected");

    input = make_rrep(9U, 3U, 2U, 5000U);
    input.destination_network_id = 0U;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "zero destination network rejected");

    input = make_rrep(9U, 4U, UINT8_MAX, 5000U);

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "hop-count overflow rejected");

    input = make_rrep(9U, 5U, 2U, 5000U);
    input.reserved_0 = 1U;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "non-zero reserved_0 rejected");

    input = make_rrep(9U, 6U, 2U, 5000U);
    input.reserved_1 = 1U;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "non-zero reserved_1 rejected");

    input = make_rrep(9U, 7U, 2U, 5000U);

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){0U, 0U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "invalid previous hop rejected");

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            NULL,
            &output) == ENP_RREP_RESULT_REJECT,
        "NULL RREP rejected");

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            NULL) == ENP_RREP_RESULT_REJECT,
        "NULL forward RREP rejected");
}

static void test_no_route(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = false,
        .completion_ok = true
    };

    setup(&processor, &mock, 3U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 10U, 2U, 5000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_DROP_NO_ROUTE,
        "RREP dropped when originator route is unavailable");

    EXPECT_TRUE(
        mock.update_calls == 1U,
        "destination route is still learned");

    EXPECT_TRUE(
        mock.lookup_calls == 1U,
        "originator lookup attempted");
}

static void test_update_failure(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = false,
        .lookup_ok = true,
        .completion_ok = true
    };

    setup(&processor, &mock, 3U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 20U, 2U, 5000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 8U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "route-update failure rejects RREP");

    EXPECT_TRUE(
        mock.lookup_calls == 0U,
        "failed route update stops processing");
}

static void test_completion_failure(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = false
    };

    setup(&processor, &mock, 1U, 1U);

    enp_routing_rrep_t input =
        make_rrep(9U, 30U, 3U, 5000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 2U},
            &input,
            &output) == ENP_RREP_RESULT_REJECT,
        "discovery completion failure rejects RREP");

    EXPECT_TRUE(
        mock.completion_calls == 1U,
        "completion callback invoked once");
}

static void test_sequence_wrap_helper_behavior(void)
{
    enp_rrep_processor_t processor;
    mock_context_t mock = {
        .update_ok = true,
        .lookup_ok = true,
        .completion_ok = true
    };

    setup(&processor, &mock, 1U, 1U);

    /*
     * R4-C does not independently reject a sequence based on age.
     * R4-A.1 performs discovery-result correlation. This test verifies that
     * R4-C preserves the wrapped sequence unchanged through completion.
     */
    enp_routing_rrep_t input =
        make_rrep(9U, 0x00000002U, 3U, 5000U);
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_rrep_processor_handle(
            &processor,
            (enp_rrep_node_t){1U, 2U},
            &input,
            &output) == ENP_RREP_RESULT_COMPLETE,
        "sequence wrap value accepted and preserved");

    EXPECT_TRUE(
        mock.last_sequence == 0x00000002U,
        "wrapped sequence preserved");
}

static void test_wire_round_trip(void)
{
    enp_routing_rrep_t input =
        make_rrep(9U, 0x12345678U, 3U, 9000U);

    uint8_t buffer[ENP_ROUTING_RREP_WIRE_SIZE];
    enp_routing_rrep_t output;

    EXPECT_TRUE(
        enp_routing_rrep_encode(
            &input,
            buffer,
            sizeof(buffer)),
        "RREP encode for processor test");

    EXPECT_TRUE(
        enp_routing_rrep_decode(
            &output,
            buffer,
            sizeof(buffer)),
        "RREP decode for processor test");

    EXPECT_TRUE(
        memcmp(&input, &output, sizeof(input)) == 0,
        "RREP wire round-trip preserved");
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "ENP v0.2 R4-C RREP processor test");
    ESP_LOGI(TAG, "======================================");

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Initialization tests");
    test_initialization();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Forwarding tests");
    test_forward();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Originator completion tests");
    test_complete_at_originator();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Invalid-input tests");
    test_invalid();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Missing-route tests");
    test_no_route();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Route-update failure tests");
    test_update_failure();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Discovery completion failure tests");
    test_completion_failure();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Sequence handling tests");
    test_sequence_wrap_helper_behavior();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Wire integration tests");
    test_wire_round_trip();

    ESP_LOGI(TAG, "======================================");

    if (s_failures == 0) {
        ESP_LOGI(TAG, "ALL RREP PROCESSOR TESTS PASSED");
    } else {
        ESP_LOGE(
            TAG,
            "%d RREP PROCESSOR TEST(S) FAILED",
            s_failures);
    }

    ESP_LOGI(TAG, "======================================");
}