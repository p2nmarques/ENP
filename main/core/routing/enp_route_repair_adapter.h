/*
 * enp_route_repair_adapter.h
 *
 *  Created on: Aug 21, 2026
 *      Author: Pedro Marques
 * 
 * ENP v0.2 — Phase 4 / P4-E5D Step 3
 * Routing-control / R4 orchestration adapter.
 */

#ifndef ENP_ROUTE_REPAIR_ADAPTER_H
#define ENP_ROUTE_REPAIR_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/enp_address.h"
#include "core/enp_duplicate.h"
#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/payloads/enp_routing.h"
#include "core/routing/enp_route_discovery.h"
#include "core/routing/enp_route_repair.h"
#include "core/routing/enp_route_table.h"
#include "core/routing/enp_rrep_processor.h"
#include "core/routing/enp_rreq_processor.h"
#include "core/routing/enp_routing_data_path.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENP_ROUTE_REPAIR_ADAPTER_DEFAULT_RREQ_LIFETIME_MS 10000U
#define ENP_ROUTE_REPAIR_ADAPTER_DEFAULT_TTL 8U
#define ENP_ROUTE_REPAIR_ADAPTER_MAX_PENDING ENP_ROUTE_REPAIR_MAX_PENDING

typedef bool (*enp_route_repair_select_next_hop_fn)(
    void *context, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop,
    enp_route_destination_t *next_hop);

typedef uint32_t (*enp_route_repair_now_ms_fn)(void *context);

typedef struct {
    enp_route_repair_t *repair;
    enp_route_table_t *routes;
    enp_transport_t *transport;

    enp_address_t local_address;

    enp_route_repair_select_next_hop_fn select_next_hop;
    void *select_next_hop_context;
    enp_routing_resolve_transport_fn resolve_transport;
    void *resolve_transport_context;
    enp_route_repair_now_ms_fn now_ms;
    void *now_ms_context;

    enp_route_discovery_t discovery;
    enp_rreq_processor_t rreq_processor;
    enp_rrep_processor_t rrep_processor;
    enp_duplicate_cache_t rreq_duplicates;

    enp_routing_rreq_t active_rreq;
    enp_route_repair_request_t pending_repairs[ENP_ROUTE_REPAIR_ADAPTER_MAX_PENDING];
    size_t pending_repair_count;
    enp_rrep_node_t active_discovery_originator;

    bool initialized;
    bool active_repair;
    enp_route_repair_request_t active_request;
    uint32_t next_route_request_id;
    uint32_t next_packet_sequence;

    uint32_t rreq_tx_count;
    uint32_t rreq_rx_count;
    uint32_t rreq_forward_count;
    uint32_t rrep_rx_count;
    uint32_t rrep_forward_count;
    uint32_t rejected_failed_next_hop_count;
} enp_route_repair_adapter_t;

bool enp_route_repair_adapter_init(
    enp_route_repair_adapter_t *adapter, enp_route_repair_t *repair,
    enp_route_table_t *routes, enp_transport_t *transport,
    enp_address_t local_address,
    enp_route_repair_select_next_hop_fn select_next_hop,
    void *select_next_hop_context,
    enp_routing_resolve_transport_fn resolve_transport,
    void *resolve_transport_context, enp_route_repair_now_ms_fn now_ms,
    void *now_ms_context);

void enp_route_repair_adapter_consume(
    const enp_route_repair_request_t *request, void *context);

enp_rreq_result_t enp_route_repair_adapter_handle_rreq(
    enp_route_repair_adapter_t *adapter, enp_rreq_node_t originator,
    enp_rreq_node_t immediate_sender, const enp_routing_rreq_t *rreq,
    enp_routing_rreq_t *forward_rreq, enp_sequence_t packet_sequence);

enp_rrep_result_t enp_route_repair_adapter_handle_rrep(
    enp_route_repair_adapter_t *adapter, enp_rrep_node_t previous_hop,
    const enp_routing_rrep_t *rrep, const enp_address_t *rrep_origin,
    enp_sequence_t packet_sequence);

bool enp_route_repair_adapter_tick(enp_route_repair_adapter_t *adapter,
                                   uint32_t now_ms);

bool enp_route_repair_adapter_is_active(
    const enp_route_repair_adapter_t *adapter);

const enp_route_repair_request_t *enp_route_repair_adapter_active_request(
    const enp_route_repair_adapter_t *adapter);

#ifdef __cplusplus
}
#endif

#endif /* ENP_ROUTE_REPAIR_ADAPTER_H */
