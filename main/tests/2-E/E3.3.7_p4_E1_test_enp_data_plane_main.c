/*
 * E3.3.7_p4_E1_test_enp_data_plane_main.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4.1 — ENP reusable data-plane self-test.
 * ESP-IDF 6.0.2 compatible.
 *
 * P4.1 validates that the reusable ENP data plane owns:
 *
 *   - receive-time packet validation;
 *   - DATA duplicate suppression;
 *   - ACK duplicate suppression in a separate domain;
 *   - non-local DATA/ACK forwarding through the existing routing path;
 *   - TTL decrement during forwarding without modifying the caller packet.
 *
 * Cached-ACK recovery remains outside P4.1 and is consolidated in a later
 * phase. The existing E3C behavior remains the reference integration test
 * for that functionality.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "core/enp_context.h"
#include "core/data/enp_data_plane.h"
#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_ack.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_routing_data_path.h"

static const char *TAG = "E3_3_7_P4_1";

#define TEST_NETWORK_ID      ((enp_network_id_t)1U)
#define TEST_LOCAL_NODE      ((enp_node_id_t)1U)
#define TEST_NEXT_HOP        ((enp_node_id_t)2U)
#define TEST_DESTINATION     ((enp_node_id_t)3U)
#define TEST_SEQUENCE        ((enp_sequence_t)0x7401U)
#define TEST_TTL             ((uint8_t)8U)

static uint8_t s_tx_buffer[ENP_MAX_FRAME_SIZE];
static size_t s_tx_length;
static uint32_t s_tx_count;
static uint8_t s_tx_destination;
static uint32_t s_local_process_count;

static esp_err_t mock_send(
        const enp_transport_address_t *destination,
        const void *data,
        size_t length)
{
    if ((destination == NULL) ||
        (data == NULL) ||
        (length > sizeof(s_tx_buffer)))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(s_tx_buffer, data, length);
    s_tx_length = length;
    ++s_tx_count;
    s_tx_destination = destination->value[0];

    return ESP_OK;
}

static bool resolve_transport(
        void *context,
        enp_route_destination_t next_hop,
        enp_transport_address_t *transport_address)
{
    (void)context;

    if (transport_address == NULL)
    {
        return false;
    }

    memset(transport_address, 0, sizeof(*transport_address));
    transport_address->length = 1U;
    transport_address->value[0] =
            (uint8_t)next_hop.node_id;

    return true;
}

static bool install_route(
        enp_route_table_t *routes)
{
    enp_route_entry_t entry = {0};

    entry.destination.network_id = TEST_NETWORK_ID;
    entry.destination.node_id = TEST_DESTINATION;
    entry.next_hop.network_id = TEST_NETWORK_ID;
    entry.next_hop.node_id = TEST_NEXT_HOP;
    entry.metric.type = ENP_ROUTE_METRIC_HOP_COUNT;
    entry.metric.value = 2U;
    entry.metric.valid = true;
    entry.route_sequence = 1U;
    entry.expires_at_ms = UINT32_MAX;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    return enp_route_table_insert(routes, &entry);
}

static bool make_packet(
        enp_packet_t *packet,
        enp_packet_type_t type,
        uint8_t ttl,
        enp_sequence_t sequence)
{
    const enp_address_t source = {
        .network = TEST_NETWORK_ID,
        .node = TEST_LOCAL_NODE,
    };

    enp_packet_init(
            packet,
            type,
            &source);

    enp_header_t *header =
            enp_packet_header(packet);

    if (header == NULL)
    {
        return false;
    }

    header->destination.network = TEST_NETWORK_ID;
    header->destination.node = TEST_DESTINATION;
    header->flags = ENP_FLAG_NONE;
    header->ttl = ttl;
    header->sequence = sequence;

    if (type == ENP_PACKET_ACK)
    {
        enp_ack_payload_t *ack =
                (enp_ack_payload_t *)enp_packet_payload(packet);

        if (ack == NULL)
        {
            return false;
        }

        enp_ack_payload_init(
                ack,
                sequence,
                0x00000001U);

        return enp_packet_seal(
                       packet,
                       ENP_ACK_WIRE_SIZE) == ESP_OK;
    }

    const uint8_t payload[] = {0xA4U, 0x01U};

    memcpy(
            enp_packet_payload(packet),
            payload,
            sizeof(payload));

    return enp_packet_seal(
                   packet,
                   sizeof(payload)) == ESP_OK;
}

static bool captured_packet(
        enp_packet_t *packet)
{
    if ((packet == NULL) ||
        (s_tx_length == 0U) ||
        (s_tx_length > sizeof(packet->buffer)))
    {
        return false;
    }

    memset(packet, 0, sizeof(*packet));
    memcpy(
            enp_packet_data(packet),
            s_tx_buffer,
            s_tx_length);

    return enp_packet_verify(packet);
}

static esp_err_t local_process(
        void *context,
        const enp_packet_t *packet,
        const enp_transport_address_t *source)
{
    (void)context;
    (void)packet;
    (void)source;
    if (packet == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    ++s_local_process_count;
    return ESP_OK;
}

void app_main(void)
{
    ESP_LOGI(TAG, "======================================");
    ESP_LOGI(TAG, "E3.3.7 PHASE 4 / P4.1 DATA PLANE SELF-TEST");
    ESP_LOGI(TAG, "Target: ESP-IDF 6.0.2");
    ESP_LOGI(TAG, "======================================");

    bool pass = true;

    static enp_context_t context = {0};
    context.network.id = TEST_NETWORK_ID;
    context.network.local.id = TEST_LOCAL_NODE;

    static enp_route_table_t routes;
    static enp_transport_t transport = {
        .send = mock_send,
    };
    static enp_routing_data_path_t routing_path;
    static enp_data_plane_t data_plane;

    if (!enp_route_table_init(&routes))
    {
        ESP_LOGE(TAG, "FAIL: route table initialized");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: route table initialized");
    }

    if (!install_route(&routes))
    {
        ESP_LOGE(TAG, "FAIL: active route installed");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: active route destination C -> next hop B installed");
    }

    if (!enp_routing_data_path_init(
                &routing_path,
                &routes,
                &transport,
                resolve_transport,
                NULL))
    {
        ESP_LOGE(TAG, "FAIL: routing data path initialized");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: routing data path initialized");
    }

    if (enp_data_plane_init(
                &data_plane,
                &context,
                &routing_path,
                local_process) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: reusable data plane initialized");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: reusable data plane initialized");
    }

    static enp_transport_address_t source = {0};
    source.length = 1U;
    source.value[0] = 9U;

    static enp_packet_t data_packet;
    static enp_packet_t ack_packet;

    if (!make_packet(
                &data_packet,
                ENP_PACKET_APPLICATION,
                TEST_TTL,
                TEST_SEQUENCE))
    {
        ESP_LOGE(TAG, "FAIL: DATA packet constructed");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: DATA packet constructed");
    }

    s_tx_count = 0U;
    s_tx_length = 0U;
    s_tx_destination = 0U;

    if (enp_data_plane_process(
                &data_plane,
                &data_packet,
                &source) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: first DATA forwarded");
        pass = false;
    }
    else if (s_tx_count != 1U || s_tx_destination != TEST_NEXT_HOP)
    {
        ESP_LOGE(TAG, "FAIL: first DATA forwarding selected wrong next hop/count");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: first DATA forwarded to next hop B");
    }

    static enp_packet_t captured;
    if (!captured_packet(&captured))
    {
        ESP_LOGE(TAG, "FAIL: captured DATA frame invalid");
        pass = false;
    }
    else
    {
        const enp_header_t *captured_header =
                enp_packet_header_const(&captured);
        const enp_header_t *original_header =
                enp_packet_header_const(&data_packet);

        if ((captured_header == NULL) ||
            (original_header == NULL) ||
            (captured_header->ttl != (uint8_t)(TEST_TTL - 1U)) ||
            (original_header->ttl != TEST_TTL) ||
            (captured_header->sequence != TEST_SEQUENCE))
        {
            ESP_LOGE(TAG, "FAIL: DATA forwarding did not preserve identity/decrement TTL");
            pass = false;
        }
        else
        {
            ESP_LOGI(TAG, "PASS: DATA forwarding decremented TTL 8 -> 7 and preserved identity");
        }
    }

    if (enp_data_plane_process(
                &data_plane,
                &data_packet,
                &source) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: duplicate DATA processing returned error");
        pass = false;
    }
    else if (s_tx_count != 1U)
    {
        ESP_LOGE(TAG, "FAIL: duplicate DATA was forwarded again");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: duplicate DATA suppressed without forwarding");
    }

    if (!make_packet(
                &ack_packet,
                ENP_PACKET_ACK,
                TEST_TTL,
                TEST_SEQUENCE))
    {
        ESP_LOGE(TAG, "FAIL: ACK packet constructed");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: ACK packet constructed with same source+sequence as DATA");
    }

    if (enp_data_plane_process(
                &data_plane,
                &ack_packet,
                &source) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: first ACK forwarded");
        pass = false;
    }
    else if (s_tx_count != 2U)
    {
        ESP_LOGE(TAG, "FAIL: ACK was suppressed by DATA duplicate domain");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: ACK forwarded independently of DATA duplicate domain");
    }

    if (enp_data_plane_process(
                &data_plane,
                &ack_packet,
                &source) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: duplicate ACK processing returned error");
        pass = false;
    }
    else if (s_tx_count != 2U)
    {
        ESP_LOGE(TAG, "FAIL: duplicate ACK was forwarded again");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: duplicate ACK suppressed independently");
    }

    static enp_packet_t local_packet;
    const enp_address_t local_address = {
        .network = TEST_NETWORK_ID,
        .node = TEST_LOCAL_NODE,
    };

    enp_packet_init(
            &local_packet,
            ENP_PACKET_APPLICATION,
            &local_address);

    enp_header_t *local_header =
            enp_packet_header(&local_packet);
    if (local_header == NULL)
    {
        ESP_LOGE(TAG, "FAIL: local packet header access");
        pass = false;
    }

    if (local_header != NULL)
    {
        local_header->destination = local_address;
        local_header->ttl = TEST_TTL;
        local_header->sequence = (enp_sequence_t)(TEST_SEQUENCE + 1U);

        const uint8_t local_payload[] = {0x4CU, 0x4FU, 0x43U};
        memcpy(
                enp_packet_payload(&local_packet),
                local_payload,
                sizeof(local_payload));

        if (enp_packet_seal(
                    &local_packet,
                    sizeof(local_payload)) != ESP_OK)
        {
            ESP_LOGE(TAG, "FAIL: local packet construction");
            pass = false;
        }
        else if (enp_data_plane_process(
                    &data_plane,
                    &local_packet,
                    &source) != ESP_OK ||
                 s_local_process_count != 1U)
        {
            ESP_LOGE(TAG, "FAIL: local packet delivery callback");
            pass = false;
        }
        else
        {
            ESP_LOGI(TAG, "PASS: local packet delivered without forwarding");
        }
    }

    if (enp_duplicate_count(
                &data_plane.data_duplicates,
                enp_context_time_ms(&context)) != 2U)
    {
        ESP_LOGE(TAG, "FAIL: DATA duplicate domain entry count");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: DATA duplicate domain contains one identity");
    }

    if (enp_duplicate_count(
                &data_plane.ack_duplicates,
                enp_context_time_ms(&context)) != 1U)
    {
        ESP_LOGE(TAG, "FAIL: ACK duplicate domain entry count");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: ACK duplicate domain contains one independent identity");
    }

    if (enp_data_plane_deinit(&data_plane) != ESP_OK)
    {
        ESP_LOGE(TAG, "FAIL: data plane deinitialized");
        pass = false;
    }
    else
    {
        ESP_LOGI(TAG, "PASS: data plane deinitialized");
    }

    if (pass)
    {
        ESP_LOGI(TAG, "--------------------------------------");
        ESP_LOGI(TAG, "E3.3.7 Phase 4 / P4.1 self-test PASS");
        ESP_LOGI(TAG, "======================================");
    }
    else
    {
        ESP_LOGE(TAG, "E3.3.7 Phase 4 / P4.1 self-test FAIL");
    }
}
