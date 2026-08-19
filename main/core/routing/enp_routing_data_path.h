/*
 * enp_routing_data_path.h
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 *
 * ENP v0.2 — E3.3.7 Phase 3 / E1 routing data-path integration.
 * ESP-IDF 6.0.2 compatible.
 */

#ifndef ENP_ROUTING_DATA_PATH_H
#define ENP_ROUTING_DATA_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/enp_transport.h"
#include "core/protocol/enp_packet.h"
#include "core/routing/enp_route_table.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Resolve the logical next hop to its transport address.
 *
 * The routing data path does not inspect neighbor-table internals. The
 * integration owner supplies this callback, preserving the routing layer's
 * transport independence.
 */
typedef bool (*enp_routing_resolve_transport_fn)(
	void *context, enp_route_destination_t next_hop,
	enp_transport_address_t *transport_address);

typedef void (*enp_routing_route_failure_fn)(
	void *context, enp_route_destination_t destination,
	enp_route_destination_t failed_next_hop);

typedef struct {
	enp_route_table_t *routes;
	enp_transport_t *transport;
	enp_routing_resolve_transport_fn resolve_transport;
	void *resolve_context;
	enp_routing_route_failure_fn route_failure;
	void *route_failure_context;
} enp_routing_data_path_t;

bool enp_routing_data_path_init(
	enp_routing_data_path_t *path, enp_route_table_t *routes,
	enp_transport_t *transport,
	enp_routing_resolve_transport_fn resolve_transport, void *resolve_context);

/*
 * Register the E5D route-repair notification boundary. The callback is
 * invoked once for each ACTIVE route transitioned to STALE by a transport
 * failure. The callback is notification-only and executes in the transport
 * send-result callback context.
 */
bool enp_routing_data_path_set_route_failure_callback(
	enp_routing_data_path_t *path, enp_routing_route_failure_fn callback,
	void *context);

/*
 * Submit an originator DATA packet using the active route to its logical
 * destination. The packet is not modified; in particular its initial TTL
 * and transaction identity remain unchanged for reliability.
 */
esp_err_t enp_routing_data_path_submit(enp_routing_data_path_t *path,
									   const enp_packet_t *packet);

/*
 * Forward an already received DATA packet to its selected next hop.
 * A local copy is modified so that the packet's TTL is decremented before
 * transmission. The caller's packet is never modified.
 */
esp_err_t enp_routing_data_path_forward(enp_routing_data_path_t *path,
										const enp_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* ENP_ROUTING_DATA_PATH_H */
