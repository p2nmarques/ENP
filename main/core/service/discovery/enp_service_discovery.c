/*
 * enp_service_discovery.c
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_service_discovery.c
 *
 * @brief ENP discovery service implementation.
 */

#include "enp_service_discovery.h"

#include <string.h>

#include "esp_log.h"

#include "core/protocol/enp_protocol.h"
#include "core/service/discovery/enp_discovery.h"
#include "core/routing/enp_routing_runtime.h"

/*----------------------------------------------------------
 * Logging
 *---------------------------------------------------------*/

static const char *TAG = "enp_discovery";

/*----------------------------------------------------------
 * Discovery Neighbor Lifecycle
 *---------------------------------------------------------*/

/*
 * Local discovery-service lifecycle classification.
 *
 * This type is intentionally private to this module because it
 * exists only to provide accurate discovery lifecycle logging.
 */
typedef enum {
    ENP_DISCOVERY_NEIGHBOR_CREATED = 0,

    ENP_DISCOVERY_NEIGHBOR_REFRESHED,

    ENP_DISCOVERY_NEIGHBOR_REACTIVATED

} enp_discovery_neighbor_event_t;

/*----------------------------------------------------------
 * Neighbor Table Diagnostics
 *---------------------------------------------------------*/

/*
 * Read back the neighbour-table entry immediately after the
 * authoritative update operation.
 *
 * This helper is diagnostic-only. It does not modify the table
 * and does not retain a pointer to an entry.
 */
static void
enp_service_discovery_log_neighbor_state(
    const enp_context_t *context,
    const enp_address_t *address)
{
    if ((context == NULL) ||
        (address == NULL)) {
        return;
    }

    const enp_neighbor_t *neighbor =
        enp_neighbor_find_const(
            &context->neighbors,
            address);

    if (neighbor == NULL) {
        ESP_LOGW(TAG,
                 "Neighbor table verification failed: "
                 "network=%u "
                 "node=%lu "
                 "not found after update",
                 (unsigned)address->network,
                 (unsigned long)address->node);

        return;
    }

    const size_t neighbor_count =
        enp_neighbor_count(
            &context->neighbors);

    ESP_LOGD(TAG,
             "Neighbor table state: "
             "network=%u "
             "node=%lu "
             "state=%u "
             "role=%u "
             "capabilities=0x%04X "
             "sequence=%lu "
             "rssi=%d "
             "count=%u",
             (unsigned)neighbor->address.network,
             (unsigned long)neighbor->address.node,
             (unsigned)neighbor->state,
             (unsigned)neighbor->role,
             (unsigned)neighbor->capabilities,
             (unsigned long)neighbor->last_sequence,
             (int)neighbor->rssi,
             (unsigned)neighbor_count);
}

/*----------------------------------------------------------
 * Direct-Neighbor Route Synchronization
 *---------------------------------------------------------*/

/*
 * Discovery owns neighbour lifecycle. Routing remains the owner of route
 * state. This integration promotes every successfully discovered direct
 * neighbour into a one-hop ACTIVE route in the production route table.
 *
 * A discovery refresh also refreshes the route lifetime. The 6000 ms
 * lifetime matches the current neighbour maintenance timeout used by the
 * production configuration, so a route stops being refreshed when the
 * corresponding neighbour stops being heard.
 */
#define ENP_DISCOVERY_DIRECT_ROUTE_LIFETIME_MS 6000U

static bool
enp_service_discovery_sync_direct_route(
    enp_context_t *context,
    const enp_address_t *neighbor_address,
    enp_sequence_t sequence,
    uint32_t now_ms)
{
    if ((context == NULL) ||
        (neighbor_address == NULL)) {
        ESP_LOGW(TAG,
                 "Direct route sync: invalid arguments");
        return false;
    }

    enp_route_table_t *const routes =
        enp_routing_runtime_route_table();

    if (routes == NULL) {
        ESP_LOGW(TAG,
                 "Direct route sync: route_table=unavailable "
                 "destination=%u/%lu",
                 (unsigned)neighbor_address->network,
                 (unsigned long)neighbor_address->node);
        return false;
    }

    enp_route_entry_t entry = {0};

    entry.destination = (enp_route_destination_t) {
        .network_id = neighbor_address->network,
        .node_id = neighbor_address->node,
    };

    entry.next_hop = entry.destination;
    entry.route_sequence = sequence;
    entry.expires_at_ms =
        now_ms + ENP_DISCOVERY_DIRECT_ROUTE_LIFETIME_MS;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    if (!enp_route_metric_init(
            &entry.metric,
            ENP_ROUTE_METRIC_HOP_COUNT)) {
        ESP_LOGW(TAG,
                 "Direct route sync: metric init failed "
                 "destination=%u/%lu",
                 (unsigned)entry.destination.network_id,
                 (unsigned long)entry.destination.node_id);
        return false;
    }

    entry.metric.value = 1U;
    entry.metric.valid = true;

    bool updated = false;
    bool result = false;

    for (size_t i = 0U; i < routes->count; ++i) {
        if ((routes->entries[i].destination.network_id ==
                 entry.destination.network_id) &&
            (routes->entries[i].destination.node_id ==
                 entry.destination.node_id)) {
            updated = true;
            result = enp_route_table_update(routes, &entry);
            break;
        }
    }

    if (!updated) {
        result = enp_route_table_insert(routes, &entry);
    }

    ESP_LOGI(TAG,
             "Direct route sync: destination=%u/%lu "
             "operation=%s "
             "result=%s "
             "route_count=%u "
             "active_route_count=%u",
             (unsigned)entry.destination.network_id,
             (unsigned long)entry.destination.node_id,
             updated ? "UPDATE" : "INSERT",
             result ? "SUCCESS" : "FAIL",
             (unsigned)enp_route_table_count(routes),
             (unsigned)enp_route_table_active_count(routes));

    return result;
}

/*----------------------------------------------------------
 * Forward Declarations
 *---------------------------------------------------------*/

static esp_err_t enp_service_discovery_init(enp_context_t *context);

static esp_err_t
enp_service_discovery_process(enp_context_t *context,
                              const enp_packet_t *packet,
                              const enp_transport_address_t *source);

/*----------------------------------------------------------
 * Service Descriptor
 *---------------------------------------------------------*/

static const enp_service_t s_discovery_service = {
    .name = "discovery",

    .packet_type = ENP_PACKET_DISCOVERY,

    .init = enp_service_discovery_init,

    .process = enp_service_discovery_process
};

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

const enp_service_t *enp_service_discovery_get(void)
{
    return &s_discovery_service;
}

/*----------------------------------------------------------
 * Service Initialization
 *---------------------------------------------------------*/

static esp_err_t enp_service_discovery_init(enp_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Discovery service initialized");

    return ESP_OK;
}

/*----------------------------------------------------------
 * Discovery Transmit
 *---------------------------------------------------------*/

esp_err_t enp_service_discovery_send(enp_context_t *context)
{
    if (context == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (context->transport == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Build the local ENP source address.
     */
    const enp_address_t source = {
        .network = context->network.id,

        .node = context->network.local.id
    };

    /*
     * Allocate the next local sequence number.
     *
     * ENP sequence number zero is reserved as the
     * uninitialized value, so the context starts at 1.
     */
    const enp_sequence_t sequence =
        context->network.local.next_sequence++;

    enp_packet_t packet;

    /*
     * Initialize the ENP packet.
     */
    enp_packet_init(&packet, ENP_PACKET_DISCOVERY, &source);

    enp_header_t *header =
        enp_packet_header(&packet);

    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Discovery is a logical broadcast packet.
     */
    header->destination.network =
        context->network.id;

    header->destination.node =
        ENP_NODE_BROADCAST;

    header->flags =
        ENP_FLAG_BROADCAST;

    header->sequence =
        sequence;

    /*
     * Build discovery payload.
     */
    enp_discovery_payload_t *payload =
        (enp_discovery_payload_t *)
            enp_packet_payload(&packet);

    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(payload, 0, sizeof(*payload));

    payload->role =
        (uint8_t)context->network.local.role;

    /*
     * Capabilities are currently not stored in the local
     * node runtime state.
     *
     * Therefore ENP v0.2 currently advertises no optional
     * capabilities.
     */
    payload->capabilities = 0U;

    payload->reserved = 0U;

    /*
     * Seal the complete frame.
     *
     * This sets payload_length and calculates CRC16.
     */
    esp_err_t err =
        enp_packet_seal(
            &packet,
            ENP_DISCOVERY_PAYLOAD_SIZE);

    if (err != ESP_OK) {
        ESP_LOGE(TAG,
                 "Failed to seal discovery packet: %s",
                 esp_err_to_name(err));

        return err;
    }

    const size_t frame_length =
        enp_packet_length(&packet);

    if (frame_length == 0U) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Zero-length transport address means broadcast.
     */
    enp_transport_address_t destination;

    memset(&destination, 0, sizeof(destination));

    destination.length = 0U;

    /*
     * Submit the complete serialized ENP frame to the
     * transport.
     */
    err =
        enp_transport_send(
            context->transport,
            &destination,
            enp_packet_data_const(&packet),
            frame_length);

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to send discovery: %s",
                 esp_err_to_name(err));

        return err;
    }

    ESP_LOGD(TAG,
             "Discovery sent: "
             "network=%u "
             "node=%lu "
             "sequence=%lu "
             "length=%u",
             (unsigned)source.network,
             (unsigned long)source.node,
             (unsigned long)sequence,
             (unsigned)frame_length);

    return ESP_OK;
}

/*----------------------------------------------------------
 * Discovery Receive
 *---------------------------------------------------------*/

static esp_err_t
enp_service_discovery_process(enp_context_t *context,
                              const enp_packet_t *packet,
                              const enp_transport_address_t *source)
{
    if ((context == NULL) ||
        (packet == NULL) ||
        (source == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    const enp_header_t *header =
        enp_packet_header_const(packet);

    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (header->payload_length !=
        ENP_DISCOVERY_PAYLOAD_SIZE) {

        ESP_LOGW(TAG,
                 "Invalid discovery payload length: %u",
                 (unsigned)header->payload_length);

        return ESP_ERR_INVALID_SIZE;
    }

    const enp_discovery_payload_t *payload =
        (const enp_discovery_payload_t *)
            enp_packet_payload_const(packet);

    if (payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Reserved field must be zero in ENP v0.2.
     */
    if (payload->reserved != 0U) {
        ESP_LOGW(TAG,
                 "Invalid discovery reserved field");

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * A broadcast logical source cannot represent an
     * individual neighbor.
     */
    if (enp_address_is_broadcast(&header->source)) {
        ESP_LOGW(TAG,
                 "Ignoring discovery from broadcast source");

        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t now_ms =
        enp_context_time_ms(context);

    /*
     * RSSI is not currently exposed by the generic transport
     * callback. Zero means unavailable.
     */
    const int8_t rssi = 0;

    /*
     * Classify the neighbour lifecycle before calling
     * enp_neighbor_update().
     *
     * The update operation may:
     *
     * - create a new neighbour;
     * - refresh an existing ACTIVE neighbour;
     * - reactivate an existing STALE neighbour.
     *
     * Therefore the pre-update state is the authoritative
     * basis for lifecycle-specific logging.
     */
    const enp_neighbor_t *existing_neighbor =
        enp_neighbor_find_const(
            &context->neighbors,
            &header->source);

    enp_discovery_neighbor_event_t neighbor_event;

    if (existing_neighbor == NULL) {

        neighbor_event =
            ENP_DISCOVERY_NEIGHBOR_CREATED;

    } else if (existing_neighbor->state ==
               ENP_NEIGHBOR_STATE_STALE) {

        neighbor_event =
            ENP_DISCOVERY_NEIGHBOR_REACTIVATED;

    } else {

        neighbor_event =
            ENP_DISCOVERY_NEIGHBOR_REFRESHED;
    }

    /*
     * The neighbour subsystem remains the sole authoritative
     * mutation boundary.
     */
    const esp_err_t err =
        enp_neighbor_update(
            &context->neighbors,
            &header->source,
            source,
            (enp_role_t)payload->role,
            payload->capabilities,
            header->sequence,
            rssi,
            now_ms);

    if (err != ESP_OK) {
        ESP_LOGW(TAG,
                 "Failed to update neighbor: %s",
                 esp_err_to_name(err));

        return err;
    }

    /*
     * Promote the successfully updated direct neighbour into the production
     * route table. Discovery does not mutate route state directly; the route
     * table remains the authoritative route-state owner.
     */
    if (!enp_service_discovery_sync_direct_route(
            context,
            &header->source,
            header->sequence,
            now_ms)) {
        ESP_LOGW(TAG,
                 "Failed to synchronize direct route: "
                 "network=%u node=%lu",
                 (unsigned)header->source.network,
                 (unsigned long)header->source.node);
    }

    /*
     * Verify the resulting neighbour-table state after the
     * authoritative update operation.
     *
     * This is diagnostic-only and does not modify lifecycle
     * semantics.
     */
    enp_service_discovery_log_neighbor_state(
        context,
        &header->source);

    /*
     * Emit lifecycle-specific discovery logging.
     *
     * Refresh events are normal heartbeat behaviour and are
     * therefore DEBUG-level to avoid INFO log flooding.
     */
    switch (neighbor_event) {

    case ENP_DISCOVERY_NEIGHBOR_CREATED:

        ESP_LOGI(TAG,
                 "Neighbor discovered: "
                 "network=%u "
                 "node=%lu "
                 "role=%u "
                 "capabilities=0x%04X",
                 (unsigned)header->source.network,
                 (unsigned long)header->source.node,
                 (unsigned)payload->role,
                 (unsigned)payload->capabilities);

        break;


    case ENP_DISCOVERY_NEIGHBOR_REFRESHED:

        ESP_LOGD(TAG,
                 "Neighbor refreshed: "
                 "network=%u "
                 "node=%lu",
                 (unsigned)header->source.network,
                 (unsigned long)header->source.node);

        break;


    case ENP_DISCOVERY_NEIGHBOR_REACTIVATED:

        ESP_LOGI(TAG,
                 "Neighbor reactivated: "
                 "network=%u "
                 "node=%lu "
                 "role=%u "
                 "capabilities=0x%04X",
                 (unsigned)header->source.network,
                 (unsigned long)header->source.node,
                 (unsigned)payload->role,
                 (unsigned)payload->capabilities);

        break;


    default:

        break;
    }

    return ESP_OK;
}