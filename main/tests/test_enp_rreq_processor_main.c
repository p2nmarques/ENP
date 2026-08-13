#include "core/routing/enp_rreq_processor.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "RREQTEST";
static int s_failures;

typedef struct {
    bool duplicate;
    bool reverse_route_ok;
    uint32_t duplicate_checks;
    uint32_t reverse_route_updates;
    enp_rreq_node_t last_originator;
    enp_rreq_node_t last_next_hop;
    uint8_t last_hop_count;
    enp_route_sequence_t last_sequence;
    uint32_t last_lifetime;
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

static bool mock_is_duplicate(
    void *context,
    enp_rreq_node_t originator,
    enp_route_request_id_t request_id)
{
    mock_context_t *mock = context;
    (void)originator;
    (void)request_id;

    ++mock->duplicate_checks;
    return mock->duplicate;
}

static bool mock_learn_reverse_route(
    void *context,
    enp_rreq_node_t originator,
    enp_rreq_node_t next_hop,
    uint8_t hop_count,
    enp_route_sequence_t destination_sequence,
    uint32_t lifetime_ms)
{
    mock_context_t *mock = context;

    ++mock->reverse_route_updates;
    mock->last_originator = originator;
    mock->last_next_hop = next_hop;
    mock->last_hop_count = hop_count;
    mock->last_sequence = destination_sequence;
    mock->last_lifetime = lifetime_ms;

    return mock->reverse_route_ok;
}

static enp_routing_rreq_t make_rreq(
    uint16_t destination_node,
    uint32_t request_id,
    uint8_t hop_count,
    uint8_t ttl)
{
    enp_routing_rreq_t rreq = {
        .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
        .subtype = ENP_ROUTING_SUBTYPE_RREQ,
        .destination_network_id = 1U,
        .destination_node_id = destination_node,
        .route_request_id = request_id,
        .destination_sequence = 100U,
        .hop_count = hop_count,
        .ttl = ttl,
        .route_lifetime_ms = 5000U
    };

    return rreq;
}

static void setup(
    enp_rreq_processor_t *processor,
    mock_context_t *mock)
{
    enp_rreq_processor_callbacks_t callbacks = {
        .context = mock,
        .is_duplicate = mock_is_duplicate,
        .learn_reverse_route = mock_learn_reverse_route
    };

    enp_rreq_node_t local = {
        .network_id = 1U,
        .node_id = 3U
    };

    enp_rreq_processor_init(processor, local, &callbacks);
}

static void test_initialization(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = true
    };

    enp_rreq_processor_callbacks_t callbacks = {
        .context = &mock,
        .is_duplicate = mock_is_duplicate,
        .learn_reverse_route = mock_learn_reverse_route
    };

    EXPECT_TRUE(
        enp_rreq_processor_init(
            &processor,
            (enp_rreq_node_t){1U, 3U},
            &callbacks),
        "processor initialization");

    EXPECT_TRUE(
        !enp_rreq_processor_init(
            NULL,
            (enp_rreq_node_t){1U, 3U},
            &callbacks),
        "initialization rejects NULL");

    EXPECT_TRUE(
        !enp_rreq_processor_init(
            &processor,
            (enp_rreq_node_t){0U, 0U},
            &callbacks),
        "invalid local node rejected");

    EXPECT_TRUE(
        !enp_rreq_processor_init(
            &processor,
            (enp_rreq_node_t){1U, 3U},
            NULL),
        "NULL callbacks rejected");
}

static void test_forward(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = true
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(9U, 0x1001U, 2U, 5U);
    enp_routing_rreq_t output;

    enp_rreq_result_t result = enp_rreq_processor_handle(
        &processor,
        (enp_rreq_node_t){1U, 1U},
        (enp_rreq_node_t){1U, 2U},
        &input,
        &output);

    EXPECT_TRUE(
        result == ENP_RREQ_RESULT_FORWARD,
        "new RREQ is forwarded");

    EXPECT_TRUE(
        output.hop_count == 3U,
        "forwarded RREQ increments hop count");

    EXPECT_TRUE(
        output.ttl == 4U,
        "forwarded RREQ decrements TTL");

    EXPECT_TRUE(
        mock.duplicate_checks == 1U,
        "duplicate check performed once");

    EXPECT_TRUE(
        mock.reverse_route_updates == 1U,
        "reverse route learned once");

    EXPECT_TRUE(
        mock.last_originator.node_id == 1U,
        "reverse route stores originator");

    EXPECT_TRUE(
        mock.last_next_hop.node_id == 2U,
        "reverse route stores immediate sender");

    EXPECT_TRUE(
        mock.last_hop_count == 3U,
        "reverse route stores incremented hop count");

    EXPECT_TRUE(
        mock.last_sequence == input.destination_sequence,
        "reverse route stores destination sequence");

    EXPECT_TRUE(
        mock.last_lifetime == input.route_lifetime_ms,
        "reverse route stores route lifetime");
}

static void test_destination_reply(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = true
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(3U, 0x2001U, 4U, 1U);
    enp_routing_rreq_t output;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REPLY,
        "destination RREQ requests RREP");

    EXPECT_TRUE(
        mock.reverse_route_updates == 1U,
        "destination RREQ still learns reverse route");
}

static void test_ttl_drop(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = true
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(9U, 0x3001U, 1U, 1U);
    enp_routing_rreq_t output;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_DROP_TTL,
        "TTL=1 RREQ is not forwarded");

    EXPECT_TRUE(
        mock.reverse_route_updates == 1U,
        "TTL drop still learns reverse route");
}

static void test_duplicate(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .duplicate = true,
        .reverse_route_ok = true
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(9U, 0x4001U, 2U, 5U);
    enp_routing_rreq_t output;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_DROP_DUPLICATE,
        "duplicate RREQ is dropped");

    EXPECT_TRUE(
        mock.reverse_route_updates == 0U,
        "duplicate does not update reverse route");
}

static void test_invalid(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = true
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(9U, 0x5001U, 2U, 5U);
    enp_routing_rreq_t output;

    input.payload_version = 0xFFU;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "invalid version rejected");

    input = make_rreq(9U, 0x5002U, 2U, 5U);
    input.subtype = ENP_ROUTING_SUBTYPE_RREP;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "invalid subtype rejected");

    input = make_rreq(9U, 0U, 2U, 5U);

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "zero request ID rejected");

    input = make_rreq(9U, 0x5003U, 2U, 0U);

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "zero TTL rejected");

    input = make_rreq(9U, 0x5004U, UINT8_MAX, 5U);

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "hop-count overflow rejected");

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){0U, 0U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "invalid originator rejected");
}

static void test_route_update_failure(void)
{
    enp_rreq_processor_t processor;
    mock_context_t mock = {
        .reverse_route_ok = false
    };

    setup(&processor, &mock);

    enp_routing_rreq_t input = make_rreq(9U, 0x6001U, 2U, 5U);
    enp_routing_rreq_t output;

    EXPECT_TRUE(
        enp_rreq_processor_handle(
            &processor,
            (enp_rreq_node_t){1U, 1U},
            (enp_rreq_node_t){1U, 2U},
            &input,
            &output) == ENP_RREQ_RESULT_REJECT,
        "reverse-route failure rejects RREQ");

    EXPECT_TRUE(
        mock.reverse_route_updates == 1U,
        "reverse-route callback invoked");
}

static void test_wire_round_trip(void)
{
    enp_routing_rreq_t input = make_rreq(9U, 0x7001U, 3U, 7U);
    uint8_t buffer[ENP_ROUTING_RREQ_WIRE_SIZE];
    enp_routing_rreq_t output;

    EXPECT_TRUE(
        enp_routing_rreq_encode(
            &input,
            buffer,
            sizeof(buffer)),
        "RREQ encode for processor test");

    EXPECT_TRUE(
        enp_routing_rreq_decode(
            &output,
            buffer,
            sizeof(buffer)),
        "RREQ decode for processor test");

    EXPECT_TRUE(
        memcmp(&input, &output, sizeof(input)) == 0,
        "RREQ wire round-trip preserved");
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "ENP v0.2 R4-B RREQ processor test");
    ESP_LOGI(TAG, "======================================");

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Initialization tests");
    test_initialization();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Forwarding tests");
    test_forward();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Destination tests");
    test_destination_reply();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "TTL tests");
    test_ttl_drop();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Duplicate tests");
    test_duplicate();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Invalid-input tests");
    test_invalid();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Route-update failure tests");
    test_route_update_failure();

    ESP_LOGI(TAG, "--------------------------------------");
    ESP_LOGI(TAG, "Wire integration tests");
    test_wire_round_trip();

    ESP_LOGI(TAG, "======================================");

    if (s_failures == 0) {
        ESP_LOGI(TAG, "ALL RREQ PROCESSOR TESTS PASSED");
    } else {
        ESP_LOGE(
            TAG,
            "%d RREQ PROCESSOR TEST(S) FAILED",
            s_failures);
    }

    ESP_LOGI(TAG, "======================================");
}
