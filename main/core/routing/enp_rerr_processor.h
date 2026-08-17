/*
 * enp_rerr_processor.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

/**
 * ENP v0.2 R4-D — RERR Processor
 *
 * Hardware-independent processing of received Route Error messages.
 *
 * R4-D owns the protocol decision:
 *   - validate RERR;
 *   - determine whether it applies to the installed route;
 *   - compare destination sequence freshness;
 *   - invalidate an applicable route.
 *
 * R4-D deliberately does not own:
 *   - route-table storage;
 *   - transport;
 *   - packet forwarding;
 *   - precursor/notification management.
 *
 * The v0.2 RERR payload identifies the unreachable destination, not an
 * originator or precursor list. Therefore forwarding/notification policy is
 * left to the integration layer rather than invented here.
 */

#ifndef ENP_RERR_PROCESSOR_H
#define ENP_RERR_PROCESSOR_H

#include <stdbool.h>
#include <stdint.h>

#include "core/protocol/payloads/enp_routing.h"

typedef struct {
	uint16_t network_id;
	uint16_t node_id;
} enp_rerr_destination_t;

typedef struct {
	bool installed;
	bool active;
	uint32_t destination_sequence;
} enp_rerr_route_info_t;

typedef bool (*enp_rerr_route_lookup_fn)(void *context,
										 enp_rerr_destination_t destination,
										 enp_rerr_route_info_t *route_info);

typedef bool (*enp_rerr_route_invalidate_fn)(
	void *context, enp_rerr_destination_t destination);

typedef struct {
	void *context;
	enp_rerr_route_lookup_fn lookup_route;
	enp_rerr_route_invalidate_fn invalidate_route;
} enp_rerr_processor_callbacks_t;

typedef struct {
	enp_rerr_processor_callbacks_t callbacks;
} enp_rerr_processor_t;

typedef enum {
	ENP_RERR_RESULT_REJECT = 0,
	ENP_RERR_RESULT_IGNORED_NO_ROUTE,
	ENP_RERR_RESULT_IGNORED_STALE,
	ENP_RERR_RESULT_IGNORED_INACTIVE,
	ENP_RERR_RESULT_INVALIDATED,
} enp_rerr_result_t;

bool enp_rerr_processor_init(enp_rerr_processor_t *processor,
							 const enp_rerr_processor_callbacks_t *callbacks);

enp_rerr_result_t enp_rerr_processor_handle(enp_rerr_processor_t *processor,
											const enp_routing_rerr_t *rerr);

#endif
