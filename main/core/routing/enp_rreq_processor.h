/*
 *  enp_rreq_processor.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

/**
 * ENP v0.2 R4-B — RREQ Processor
 *
 * Hardware-independent RREQ processing.
 *
 * The processor does not own:
 *   - transport
 *   - duplicate cache storage
 *   - route-table storage
 *   - RREP transmission
 *
 * Those are supplied through callbacks.
 */

#ifndef ENP_RREQ_PROCESSOR_H
#define ENP_RREQ_PROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "core/protocol/payloads/enp_routing.h"

typedef struct {
	uint16_t network_id;
	uint16_t node_id;
} enp_rreq_node_t;

typedef enum {
	ENP_RREQ_RESULT_REJECT = 0,
	ENP_RREQ_RESULT_DROP_DUPLICATE,
	ENP_RREQ_RESULT_DROP_TTL,
	ENP_RREQ_RESULT_REPLY,
	ENP_RREQ_RESULT_FORWARD,
} enp_rreq_result_t;

typedef bool (*enp_rreq_duplicate_check_fn)(void *context,
											enp_rreq_node_t originator,
											enp_route_request_id_t request_id);

typedef bool (*enp_rreq_reverse_route_fn)(
	void *context, enp_rreq_node_t originator, enp_rreq_node_t next_hop,
	uint8_t hop_count, enp_route_sequence_t destination_sequence,
	uint32_t lifetime_ms);

typedef struct {
	void *context;
	enp_rreq_duplicate_check_fn is_duplicate;
	enp_rreq_reverse_route_fn learn_reverse_route;
} enp_rreq_processor_callbacks_t;

typedef struct {
	enp_rreq_node_t local_node;
	enp_rreq_processor_callbacks_t callbacks;
} enp_rreq_processor_t;

bool enp_rreq_processor_init(enp_rreq_processor_t *processor,
							 enp_rreq_node_t local_node,
							 const enp_rreq_processor_callbacks_t *callbacks);

enp_rreq_result_t enp_rreq_processor_handle(enp_rreq_processor_t *processor,
											enp_rreq_node_t originator,
											enp_rreq_node_t immediate_sender,
											const enp_routing_rreq_t *rreq,
											enp_routing_rreq_t *forward_rreq);

#endif
