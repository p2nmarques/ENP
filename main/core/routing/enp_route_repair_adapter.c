/*
 * enp_route_repair_adapter.c
 *
 *  Created on: Aug 21, 2026
 *      Author: Pedro Marques
 * 
 * ENP v0.2 — Phase 4 / P4-E5D Step 3
 * Routing-control / R4 orchestration adapter.
 */

#include "enp_route_repair_adapter.h"

#include <string.h>

static bool destination_equal(enp_route_destination_t lhs,
                              enp_route_destination_t rhs) {
    return lhs.network_id == rhs.network_id && lhs.node_id == rhs.node_id;
}

static enp_route_destination_t rreq_to_route(enp_rreq_node_t node) {
    return (enp_route_destination_t){
        .network_id = node.network_id, .node_id = node.node_id};
}

static enp_route_destination_t rrep_to_route(enp_rrep_node_t node) {
    return (enp_route_destination_t){
        .network_id = node.network_id, .node_id = node.node_id};
}

static enp_rreq_node_t route_to_rreq(enp_route_destination_t node) {
    return (enp_rreq_node_t){
        .network_id = node.network_id, .node_id = node.node_id};
}

static enp_rrep_node_t route_to_rrep(enp_route_destination_t node) {
    return (enp_rrep_node_t){
        .network_id = node.network_id, .node_id = node.node_id};
}

static uint32_t adapter_now_ms(const enp_route_repair_adapter_t *adapter) {
    return (adapter != NULL && adapter->now_ms != NULL)
               ? adapter->now_ms(adapter->now_ms_context)
               : 0U;
}

static uint32_t allocate_packet_sequence(enp_route_repair_adapter_t *adapter) {
    uint32_t sequence = adapter->next_packet_sequence;
    if (sequence == 0U) {
        sequence = 1U;
    }
    ++adapter->next_packet_sequence;
    if (adapter->next_packet_sequence == 0U) {
        adapter->next_packet_sequence = 1U;
    }
    return sequence;
}

static uint32_t allocate_route_request_id(
    enp_route_repair_adapter_t *adapter) {
    uint32_t id = adapter->next_route_request_id;
    if (id == 0U) {
        id = 1U;
    }
    ++adapter->next_route_request_id;
    if (adapter->next_route_request_id == 0U) {
        adapter->next_route_request_id = 1U;
    }
    return id;
}

static bool sequence_newer_or_equal(uint32_t received, uint32_t requested) {
    const uint32_t difference = received - requested;
    return difference == 0U || difference < 0x80000000UL;
}

static bool lookup_route_any_state(const enp_route_table_t *routes,
                                   enp_route_destination_t destination,
                                   enp_route_entry_t *entry) {
    if (routes == NULL || entry == NULL) {
        return false;
    }
    for (size_t i = 0U; i < routes->count; ++i) {
        if (destination_equal(routes->entries[i].destination, destination)) {
            *entry = routes->entries[i];
            return true;
        }
    }
    return false;
}

static bool lookup_active_next_hop(void *context,
                                   enp_rrep_node_t destination,
                                   enp_rrep_node_t *next_hop) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL || adapter->routes == NULL || next_hop == NULL) {
        return false;
    }

    const enp_route_entry_t *entry = enp_route_table_lookup_const(
        adapter->routes, rrep_to_route(destination));
    if (entry == NULL || entry->state != ENP_ROUTE_STATE_ACTIVE) {
        return false;
    }

    *next_hop = route_to_rrep(entry->next_hop);
    return true;
}

static bool install_rrep_route(void *context, enp_rrep_node_t destination,
                               enp_rrep_node_t next_hop,
                               enp_route_sequence_t destination_sequence,
                               uint8_t hop_count, uint32_t lifetime_ms,
                               uint32_t metric) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL || adapter->routes == NULL) {
        return false;
    }

    const enp_route_destination_t dst = rrep_to_route(destination);
    const enp_route_destination_t nh = rrep_to_route(next_hop);

    if (adapter->active_repair && destination_equal(dst, adapter->active_request.destination)) {
        if (destination_equal(nh, adapter->active_request.failed_next_hop)) {
            ++adapter->rejected_failed_next_hop_count;
            return false;
        }
        if (!sequence_newer_or_equal(destination_sequence,
                                     adapter->discovery.destination_sequence)) {
            return false;
        }
    }

    enp_route_entry_t entry = {0};
    entry.destination = dst;
    entry.next_hop = nh;
    entry.route_sequence = destination_sequence;
    entry.expires_at_ms = adapter_now_ms(adapter) + lifetime_ms;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    if (!enp_route_metric_init(&entry.metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
        return false;
    }
    const uint32_t effective_metric = metric == 0U ? hop_count : metric;
    if (effective_metric > UINT16_MAX) {
        return false;
    }
    entry.metric.value = (uint16_t)effective_metric;
    entry.metric.valid = true;

    for (size_t i = 0U; i < adapter->routes->count; ++i) {
        if (destination_equal(adapter->routes->entries[i].destination, dst)) {
            return enp_route_table_update(adapter->routes, &entry);
        }
    }
    return enp_route_table_insert(adapter->routes, &entry);
}

static bool discovery_complete(void *context,
                               enp_rrep_node_t destination,
                               enp_route_sequence_t destination_sequence) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL || !adapter->active_repair ||
        !destination_equal(rrep_to_route(destination),
                           adapter->active_request.destination)) {
        return false;
    }

    return enp_route_discovery_on_rrep(
        &adapter->discovery,
        (enp_discovery_destination_t){.network_id = destination.network_id,
                                      .node_id = destination.node_id},
        destination_sequence);
}

static bool rreq_duplicate(void *context, enp_rreq_node_t originator,
                           enp_route_request_id_t request_id) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL) {
        return true;
    }

    const enp_address_t source = {
        .network = originator.network_id, .node = originator.node_id};
    bool duplicate = false;
    if (enp_duplicate_check_and_record(
            &adapter->rreq_duplicates, &source, request_id,
            adapter_now_ms(adapter), &duplicate) != ESP_OK) {
        return true;
    }
    return duplicate;
}

static bool learn_reverse_route(void *context, enp_rreq_node_t originator,
                                enp_rreq_node_t next_hop, uint8_t hop_count,
                                enp_route_sequence_t destination_sequence,
                                uint32_t lifetime_ms) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL || adapter->routes == NULL) {
        return false;
    }

    enp_route_entry_t entry = {0};
    entry.destination = rreq_to_route(originator);
    entry.next_hop = rreq_to_route(next_hop);
    entry.route_sequence = destination_sequence;
    entry.expires_at_ms = adapter_now_ms(adapter) + lifetime_ms;
    entry.state = ENP_ROUTE_STATE_ACTIVE;

    if (!enp_route_metric_init(&entry.metric, ENP_ROUTE_METRIC_HOP_COUNT)) {
        return false;
    }
    entry.metric.value = hop_count;
    entry.metric.valid = true;

    for (size_t i = 0U; i < adapter->routes->count; ++i) {
        if (destination_equal(adapter->routes->entries[i].destination,
                              entry.destination)) {
            return enp_route_table_update(adapter->routes, &entry);
        }
    }
    return enp_route_table_insert(adapter->routes, &entry);
}

static esp_err_t send_payload(enp_route_repair_adapter_t *adapter,
                              const enp_address_t *source,
                              enp_route_destination_t logical_destination,
                              enp_sequence_t packet_sequence, uint8_t ttl,
                              const void *payload, size_t payload_size,
                              size_t wire_size,
                              const enp_transport_address_t *transport_address) {
    if (adapter == NULL || adapter->transport == NULL || source == NULL ||
        payload == NULL || transport_address == NULL || packet_sequence == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    enp_packet_t packet;
    enp_packet_init(&packet, ENP_PACKET_ROUTE, source);
    enp_header_t *header = enp_packet_header(&packet);
    if (header == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    header->destination.network = logical_destination.network_id;
    header->destination.node = logical_destination.node_id;
    header->flags = ENP_FLAG_NONE;
    header->sequence = packet_sequence;
    header->ttl = ttl;

    memcpy(enp_packet_payload(&packet), payload, payload_size);

    esp_err_t err = enp_packet_seal(&packet, wire_size);
    if (err != ESP_OK) {
        return err;
    }

    return enp_transport_send(adapter->transport, transport_address,
                              enp_packet_data_const(&packet),
                              enp_packet_length(&packet));
}

static bool select_and_resolve(enp_route_repair_adapter_t *adapter,
                               enp_route_destination_t destination,
                               enp_route_destination_t failed_next_hop,
                               enp_route_destination_t *next_hop,
                               enp_transport_address_t *transport_address) {
    if (adapter == NULL || adapter->select_next_hop == NULL ||
        adapter->resolve_transport == NULL || next_hop == NULL ||
        transport_address == NULL) {
        return false;
    }

    if (!adapter->select_next_hop(adapter->select_next_hop_context, destination,
                                  failed_next_hop, next_hop)) {
        return false;
    }
    if (next_hop->network_id == 0U || next_hop->node_id == 0U ||
        destination_equal(*next_hop, failed_next_hop)) {
        return false;
    }
    return adapter->resolve_transport(adapter->resolve_transport_context,
                                      *next_hop, transport_address);
}

static bool send_active_rreq(enp_route_repair_adapter_t *adapter) {
    enp_route_destination_t next_hop = {0};
    enp_transport_address_t address = {0};

    if (!select_and_resolve(adapter, adapter->active_request.destination,
                            adapter->active_request.failed_next_hop, &next_hop,
                            &address)) {
        return false;
    }

    const esp_err_t err = send_payload(
        adapter, &adapter->local_address, adapter->active_request.destination,
        allocate_packet_sequence(adapter), adapter->active_rreq.ttl,
        &adapter->active_rreq, sizeof(adapter->active_rreq),
        ENP_ROUTING_RREQ_WIRE_SIZE, &address);

    if (err == ESP_OK) {
        ++adapter->rreq_tx_count;
        return true;
    }
    return false;
}

bool enp_route_repair_adapter_init(
    enp_route_repair_adapter_t *adapter, enp_route_repair_t *repair,
    enp_route_table_t *routes, enp_transport_t *transport,
    enp_address_t local_address,
    enp_route_repair_select_next_hop_fn select_next_hop,
    void *select_next_hop_context,
    enp_routing_resolve_transport_fn resolve_transport,
    void *resolve_transport_context, enp_route_repair_now_ms_fn now_ms,
    void *now_ms_context) {
    if (adapter == NULL || repair == NULL || routes == NULL ||
        transport == NULL || select_next_hop == NULL ||
        resolve_transport == NULL || now_ms == NULL || local_address.network == 0U ||
        local_address.node == 0U) {
        return false;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->repair = repair;
    adapter->routes = routes;
    adapter->transport = transport;
    adapter->local_address = local_address;
    adapter->select_next_hop = select_next_hop;
    adapter->select_next_hop_context = select_next_hop_context;
    adapter->resolve_transport = resolve_transport;
    adapter->resolve_transport_context = resolve_transport_context;
    adapter->now_ms = now_ms;
    adapter->now_ms_context = now_ms_context;
    adapter->next_route_request_id = 1U;
    adapter->next_packet_sequence = 1U;

    if (!enp_route_discovery_init(&adapter->discovery) ||
        enp_duplicate_cache_init(&adapter->rreq_duplicates) != ESP_OK) {
        return false;
    }

    const enp_rreq_processor_callbacks_t rreq_callbacks = {
        .context = adapter,
        .is_duplicate = rreq_duplicate,
        .learn_reverse_route = learn_reverse_route};

    if (!enp_rreq_processor_init(&adapter->rreq_processor,
                                 route_to_rreq((enp_route_destination_t){
                                     .network_id = local_address.network,
                                     .node_id = local_address.node}),
                                 &rreq_callbacks)) {
        return false;
    }

    const enp_rrep_processor_callbacks_t rrep_callbacks = {
        .context = adapter,
        .update_route = install_rrep_route,
        .lookup_next_hop = lookup_active_next_hop,
        .discovery_complete = discovery_complete};

    if (!enp_rrep_processor_init(
            &adapter->rrep_processor,
            route_to_rrep((enp_route_destination_t){
                .network_id = local_address.network,
                .node_id = local_address.node}),
            route_to_rrep((enp_route_destination_t){
                .network_id = local_address.network,
                .node_id = local_address.node}),
            &rrep_callbacks)) {
        return false;
    }

    adapter->initialized = true;
    return true;
}

static bool adapter_start_request(enp_route_repair_adapter_t *adapter,
                                   const enp_route_repair_request_t *request) {
    if (adapter == NULL || request == NULL || adapter->active_repair) {
        return false;
    }

    enp_route_entry_t previous = {0};
    if (!lookup_route_any_state(adapter->routes, request->destination,
                                &previous)) {
        return false;
    }

    adapter->active_request = *request;
    adapter->active_repair = true;

    const uint32_t route_request_id = allocate_route_request_id(adapter);
    const uint32_t now_ms = adapter_now_ms(adapter);

    if (!enp_route_discovery_start(
            &adapter->discovery,
            (enp_discovery_destination_t){
                .network_id = request->destination.network_id,
                .node_id = request->destination.node_id},
            route_request_id, previous.route_sequence,
            ENP_ROUTE_REPAIR_ADAPTER_DEFAULT_TTL, now_ms)) {
        adapter->active_repair = false;
        return false;
    }

    adapter->active_rreq = (enp_routing_rreq_t){
        .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
        .subtype = ENP_ROUTING_SUBTYPE_RREQ,
        .destination_network_id = request->destination.network_id,
        .destination_node_id = request->destination.node_id,
        .route_request_id = route_request_id,
        .destination_sequence = previous.route_sequence,
        .hop_count = 0U,
        .ttl = ENP_ROUTE_REPAIR_ADAPTER_DEFAULT_TTL,
        .route_lifetime_ms = ENP_ROUTE_REPAIR_ADAPTER_DEFAULT_RREQ_LIFETIME_MS};

    adapter->active_discovery_originator = route_to_rrep((enp_route_destination_t){
        .network_id = adapter->local_address.network,
        .node_id = adapter->local_address.node});
    adapter->rrep_processor.discovery_originator =
        adapter->active_discovery_originator;

    if (!send_active_rreq(adapter)) {
        adapter->active_repair = false;
        return false;
    }

    return true;
}

static bool pending_request_equal(const enp_route_repair_request_t *lhs,
                                  const enp_route_repair_request_t *rhs) {
    return lhs != NULL && rhs != NULL &&
           destination_equal(lhs->destination, rhs->destination);
}

static void adapter_remove_pending_index(enp_route_repair_adapter_t *adapter,
                                         size_t index) {
    if (adapter == NULL || index >= adapter->pending_repair_count) {
        return;
    }
    if (index + 1U < adapter->pending_repair_count) {
        memmove(&adapter->pending_repairs[index],
                &adapter->pending_repairs[index + 1U],
                (adapter->pending_repair_count - index - 1U) *
                    sizeof(adapter->pending_repairs[0]));
    }
    --adapter->pending_repair_count;
}

static bool adapter_queue_pending(enp_route_repair_adapter_t *adapter,
                                  const enp_route_repair_request_t *request) {
    if (adapter == NULL || request == NULL) {
        return false;
    }
    if (pending_request_equal(&adapter->active_request, request) &&
        adapter->active_repair) {
        return false;
    }
    for (size_t i = 0U; i < adapter->pending_repair_count; ++i) {
        if (pending_request_equal(&adapter->pending_repairs[i], request)) {
            return false;
        }
    }
    if (adapter->pending_repair_count >= ENP_ROUTE_REPAIR_ADAPTER_MAX_PENDING) {
        return false;
    }
    adapter->pending_repairs[adapter->pending_repair_count++] = *request;
    return true;
}

static void adapter_start_next_pending(enp_route_repair_adapter_t *adapter) {
    if (adapter == NULL || adapter->active_repair ||
        adapter->pending_repair_count == 0U) {
        return;
    }

    enp_route_repair_request_t request = adapter->pending_repairs[0];
    adapter_remove_pending_index(adapter, 0U);
    (void)adapter_start_request(adapter, &request);
}

void enp_route_repair_adapter_consume(
    const enp_route_repair_request_t *request, void *context) {
    enp_route_repair_adapter_t *adapter = context;
    if (adapter == NULL || !adapter->initialized || request == NULL) {
        return;
    }

    if (adapter->active_repair) {
        (void)adapter_queue_pending(adapter, request);
        return;
    }

    (void)adapter_start_request(adapter, request);
}

enp_rreq_result_t enp_route_repair_adapter_handle_rreq(
    enp_route_repair_adapter_t *adapter, enp_rreq_node_t originator,
    enp_rreq_node_t immediate_sender, const enp_routing_rreq_t *rreq,
    enp_routing_rreq_t *forward_rreq, enp_sequence_t packet_sequence) {
    if (adapter == NULL || !adapter->initialized || rreq == NULL ||
        forward_rreq == NULL) {
        return ENP_RREQ_RESULT_REJECT;
    }

    enp_rreq_result_t result = enp_rreq_processor_handle(
        &adapter->rreq_processor, originator, immediate_sender, rreq,
        forward_rreq);
    ++adapter->rreq_rx_count;

    if (result == ENP_RREQ_RESULT_REPLY) {
        enp_routing_rrep_t rrep = {
            .payload_version = ENP_ROUTING_PAYLOAD_VERSION,
            .subtype = ENP_ROUTING_SUBTYPE_RREP,
            .destination_network_id = rreq->destination_network_id,
            .destination_node_id = rreq->destination_node_id,
            .destination_sequence = rreq->destination_sequence,
            .hop_count = 1U,
            .reserved_0 = 0U,
            .route_lifetime_ms = rreq->route_lifetime_ms,
            .reserved_1 = 0U};

        enp_transport_address_t address = {0};
        if (!adapter->resolve_transport(
                adapter->resolve_transport_context,
                rreq_to_route(immediate_sender), &address)) {
            return ENP_RREQ_RESULT_REJECT;
        }

        if (send_payload(adapter, &adapter->local_address,
                         rreq_to_route(immediate_sender),
                         allocate_packet_sequence(adapter), ENP_DEFAULT_TTL,
                         &rrep, sizeof(rrep), ENP_ROUTING_RREP_WIRE_SIZE,
                         &address) != ESP_OK) {
            return ENP_RREQ_RESULT_REJECT;
        }
    } else if (result == ENP_RREQ_RESULT_FORWARD) {
        ++adapter->rreq_forward_count;

        enp_route_destination_t next_hop = {0};
        enp_transport_address_t address = {0};
        if (!select_and_resolve(
                adapter,
                (enp_route_destination_t){.network_id = rreq->destination_network_id,
                                          .node_id = rreq->destination_node_id},
                (enp_route_destination_t){0}, &next_hop, &address)) {
            return ENP_RREQ_RESULT_REJECT;
        }

        if (send_payload(adapter,
                         &(enp_address_t){.network = originator.network_id,
                                          .node = originator.node_id},
                         (enp_route_destination_t){
                             .network_id = rreq->destination_network_id,
                             .node_id = rreq->destination_node_id},
                         packet_sequence, forward_rreq->ttl, forward_rreq,
                         sizeof(*forward_rreq), ENP_ROUTING_RREQ_WIRE_SIZE,
                         &address) != ESP_OK) {
            return ENP_RREQ_RESULT_REJECT;
        }
        (void)next_hop;
    }

    return result;
}

enp_rrep_result_t enp_route_repair_adapter_handle_rrep(
    enp_route_repair_adapter_t *adapter, enp_rrep_node_t previous_hop,
    const enp_routing_rrep_t *rrep, const enp_address_t *rrep_origin,
    enp_sequence_t packet_sequence) {
    if (adapter == NULL || !adapter->initialized || rrep == NULL ||
        rrep_origin == NULL) {
        return ENP_RREP_RESULT_REJECT;
    }

    enp_routing_rrep_t forward_rrep;
    enp_rrep_result_t result = enp_rrep_processor_handle(
        &adapter->rrep_processor, previous_hop, rrep, &forward_rrep);
    ++adapter->rrep_rx_count;

    if (result == ENP_RREP_RESULT_COMPLETE) {
        const enp_route_entry_t *route = enp_route_table_lookup_const(
            adapter->routes, adapter->active_request.destination);
        if (route == NULL || route->state != ENP_ROUTE_STATE_ACTIVE ||
            destination_equal(route->next_hop,
                              adapter->active_request.failed_next_hop)) {
            return ENP_RREP_RESULT_REJECT;
        }
        adapter->active_repair = false;
        adapter_start_next_pending(adapter);
    } else if (result == ENP_RREP_RESULT_FORWARD) {
        enp_rrep_node_t next_hop;
        if (!lookup_active_next_hop(
                adapter, adapter->rrep_processor.discovery_originator,
                &next_hop)) {
            return ENP_RREP_RESULT_DROP_NO_ROUTE;
        }

        enp_transport_address_t address = {0};
        if (!adapter->resolve_transport(
                adapter->resolve_transport_context, rrep_to_route(next_hop),
                &address)) {
            return ENP_RREP_RESULT_DROP_NO_ROUTE;
        }

        if (send_payload(
                adapter, rrep_origin, rrep_to_route(next_hop), packet_sequence,
                ENP_DEFAULT_TTL, &forward_rrep, sizeof(forward_rrep),
                ENP_ROUTING_RREP_WIRE_SIZE, &address) != ESP_OK) {
            return ENP_RREP_RESULT_REJECT;
        }
        ++adapter->rrep_forward_count;
    }

    return result;
}

bool enp_route_repair_adapter_tick(enp_route_repair_adapter_t *adapter,
                                   uint32_t now_ms) {
    if (adapter == NULL || !adapter->initialized || !adapter->active_repair) {
        return false;
    }

    if (enp_route_discovery_on_timeout(&adapter->discovery, now_ms)) {
        return send_active_rreq(adapter);
    }

    if (enp_route_discovery_state(&adapter->discovery) ==
        ENP_DISCOVERY_STATE_FAILED) {
        adapter->active_repair = false;
        adapter_start_next_pending(adapter);
    }
    return false;
}

bool enp_route_repair_adapter_is_active(
    const enp_route_repair_adapter_t *adapter) {
    return adapter != NULL && adapter->active_repair &&
           enp_route_discovery_is_active(&adapter->discovery);
}

const enp_route_repair_request_t *enp_route_repair_adapter_active_request(
    const enp_route_repair_adapter_t *adapter) {
    if (adapter == NULL || !adapter->active_repair) {
        return NULL;
    }
    return &adapter->active_request;
}
