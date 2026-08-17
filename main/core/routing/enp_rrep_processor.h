/*
 * enp_rrep_processor.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

/**
 * ENP v0.2 R4-C — RREP Processor
 *
 * Hardware-independent RREP processing.
 *
 * The processor does not own:
 *   - transport
 *   - route-table storage
 *   - RREQ/RREP transmission
 *   - discovery-state storage
 *
 * These responsibilities are supplied through callbacks.
 */

#ifndef ENP_RREP_PROCESSOR_H
#define ENP_RREP_PROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "core/protocol/payloads/enp_routing.h"

typedef struct {
	uint16_t network_id;
	uint16_t node_id;
} enp_rrep_node_t;

typedef enum {
	ENP_RREP_RESULT_REJECT = 0,
	ENP_RREP_RESULT_COMPLETE,
	ENP_RREP_RESULT_FORWARD,
	ENP_RREP_RESULT_DROP_NO_ROUTE,
	ENP_RREP_RESULT_DROP_TTL,
} enp_rrep_result_t;

typedef bool (*enp_rrep_route_update_fn)(
	void *context, enp_rrep_node_t destination, enp_rrep_node_t next_hop,
	enp_route_sequence_t destination_sequence, uint8_t hop_count,
	uint32_t lifetime_ms, uint32_t metric);

typedef bool (*enp_rrep_next_hop_lookup_fn)(void *context,
											enp_rrep_node_t destination,
											enp_rrep_node_t *next_hop);

typedef bool (*enp_rrep_discovery_complete_fn)(
	void *context, enp_rrep_node_t destination,
	enp_route_sequence_t destination_sequence);

typedef struct {
	void *context;
	enp_rrep_route_update_fn update_route;
	enp_rrep_next_hop_lookup_fn lookup_next_hop;
	enp_rrep_discovery_complete_fn discovery_complete;
} enp_rrep_processor_callbacks_t;

typedef struct {
	enp_rrep_node_t local_node;
	enp_rrep_node_t discovery_originator;
	enp_rrep_processor_callbacks_t callbacks;
} enp_rrep_processor_t;

bool enp_rrep_processor_init(enp_rrep_processor_t *processor,
							 enp_rrep_node_t local_node,
							 enp_rrep_node_t discovery_originator,
							 const enp_rrep_processor_callbacks_t *callbacks);

enp_rrep_result_t enp_rrep_processor_handle(enp_rrep_processor_t *processor,
											enp_rrep_node_t previous_hop,
											const enp_routing_rrep_t *rrep,
											enp_routing_rrep_t *forward_rrep);

#endif
