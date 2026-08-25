#ifndef ENP_ROUTE_TABLE_H
#define ENP_ROUTE_TABLE_H

/**
 * ENP v0.2 Route Table Core — R3-B
 *
 * Fixed-size, statically allocated route table.
 *
 * R3-B deliberately contains no:
 *   - RREQ/RREP processing
 *   - forwarding
 *   - transport dependencies
 *   - dynamic allocation
 *   - FreeRTOS dependencies
 *
 * Route selection is based on the R3-A metric abstraction.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "enp_route_metric.h"
#include "config/enp_defaults.h"

typedef uint16_t enp_route_network_id_t;
typedef uint16_t enp_route_node_id_t;

typedef enum {
	ENP_ROUTE_STATE_INVALID = 0,
	ENP_ROUTE_STATE_ACTIVE = 1,
	ENP_ROUTE_STATE_STALE = 2,
} enp_route_state_t;

typedef struct {
	enp_route_network_id_t network_id;
	enp_route_node_id_t node_id;
} enp_route_destination_t;

typedef struct {
	enp_route_destination_t destination;
	enp_route_destination_t next_hop;

	enp_route_metric_t metric;

	uint32_t route_sequence;
	uint32_t expires_at_ms;

	enp_route_state_t state;
} enp_route_entry_t;

typedef struct {
	enp_route_entry_t entries[ENP_MAX_ROUTES];
	size_t count;
} enp_route_table_t;

/* Lifecycle */
bool enp_route_table_init(enp_route_table_t *table);

/* Lookup */
enp_route_entry_t *enp_route_table_lookup(enp_route_table_t *table,
										  enp_route_destination_t destination);

const enp_route_entry_t *
enp_route_table_lookup_const(const enp_route_table_t *table,
							 enp_route_destination_t destination);

/* Insert/update */
bool enp_route_table_insert(enp_route_table_t *table,
							const enp_route_entry_t *entry);

/*
 * Update an existing route.
 *
 * The destination must already exist.
 * Returns false if the destination is not present or the entry is invalid.
 */
bool enp_route_table_update(enp_route_table_t *table,
							const enp_route_entry_t *entry);

/* State management */
bool enp_route_table_invalidate(enp_route_table_t *table,
								enp_route_destination_t destination);

bool enp_route_table_remove(enp_route_table_t *table,
							enp_route_destination_t destination);

/*
 * Expire active routes whose expiry time has been reached.
 *
 * Returns the number of routes transitioned to STALE.
 */
size_t enp_route_table_expire(enp_route_table_t *table, uint32_t now_ms);

/* Information */
size_t enp_route_table_count(const enp_route_table_t *table);

size_t enp_route_table_active_count(const enp_route_table_t *table);

#endif /* ENP_ROUTE_TABLE_H */
