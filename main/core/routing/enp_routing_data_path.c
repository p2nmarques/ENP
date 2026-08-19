/*
 * enp_routing_data_path.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 *
 * ENP v0.2 — E3.3.7 Phase 3 / E1 routing data-path integration.
 * ESP-IDF 6.0.2 compatible.
 */

#include "enp_routing_data_path.h"

#include "core/protocol/enp_packet.h"

#include <string.h>

static bool transport_address_equal(const enp_transport_address_t *lhs,
									const enp_transport_address_t *rhs) {
	if (lhs == NULL || rhs == NULL || lhs->length != rhs->length) {
		return false;
	}

	return memcmp(lhs->value, rhs->value, lhs->length) == 0;
}

static void
transport_send_result_callback(const enp_transport_address_t *destination,
							   esp_err_t result, void *context) {
	enp_routing_data_path_t *path = (enp_routing_data_path_t *)context;

	if (path == NULL || path->routes == NULL || destination == NULL ||
		result == ESP_OK || path->resolve_transport == NULL) {
		return;
	}

	/*
	 * The transport reports only a transport address. Resolve each active
	 * logical next-hop through the existing routing integration boundary and
	 * invalidate every route whose next-hop resolves to the failed address.
	 * No transport or reliability policy is implemented here.
	 */
	for (size_t i = 0U; i < path->routes->count; ++i) {
		enp_route_entry_t *entry = &path->routes->entries[i];

		if (entry->state != ENP_ROUTE_STATE_ACTIVE) {
			continue;
		}

		enp_transport_address_t resolved = {0};
		if (!path->resolve_transport(path->resolve_context, entry->next_hop,
									 &resolved)) {
			continue;
		}

		if (transport_address_equal(&resolved, destination)) {
			const enp_route_destination_t failed_next_hop = entry->next_hop;
			if (enp_route_table_invalidate(path->routes, entry->destination) &&
				path->route_failure != NULL) {
				path->route_failure(path->route_failure_context,
									entry->destination, failed_next_hop);
			}
		}
	}
}

static bool lookup_active_route(const enp_routing_data_path_t *path,
								const enp_address_t *destination,
								enp_route_entry_t *entry) {
	if (path == NULL || path->routes == NULL || destination == NULL ||
		entry == NULL) {
		return false;
	}

	const enp_route_entry_t *found = enp_route_table_lookup_const(
		path->routes,
		(enp_route_destination_t){.network_id = destination->network,
								  .node_id = destination->node});

	if (found == NULL || found->state != ENP_ROUTE_STATE_ACTIVE) {
		return false;
	}

	*entry = *found;
	return true;
}

static esp_err_t transmit_to_next_hop(const enp_routing_data_path_t *path,
									  const enp_route_entry_t *route,
									  const enp_packet_t *packet) {
	if (path == NULL || route == NULL || packet == NULL ||
		path->transport == NULL || path->resolve_transport == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	enp_transport_address_t transport_address = {0};
	if (!path->resolve_transport(path->resolve_context, route->next_hop,
								 &transport_address)) {
		return ESP_ERR_NOT_FOUND;
	}

	return enp_transport_send(path->transport, &transport_address,
							  enp_packet_data_const(packet),
							  enp_packet_length(packet));
}

bool enp_routing_data_path_init(
	enp_routing_data_path_t *path, enp_route_table_t *routes,
	enp_transport_t *transport,
	enp_routing_resolve_transport_fn resolve_transport, void *resolve_context) {
	if (path == NULL || routes == NULL || transport == NULL ||
		transport->send == NULL || resolve_transport == NULL) {
		return false;
	}

	if (transport->set_send_result_callback == NULL) {
		return false;
	}

	path->routes = routes;
	path->transport = transport;
	path->resolve_transport = resolve_transport;
	path->resolve_context = resolve_context;

	if (enp_transport_set_send_result_callback(
			transport, transport_send_result_callback, path) != ESP_OK) {
		path->routes = NULL;
		path->transport = NULL;
		path->resolve_transport = NULL;
		path->resolve_context = NULL;
		return false;
	}

	return true;
}

esp_err_t enp_routing_data_path_submit(enp_routing_data_path_t *path,
									   const enp_packet_t *packet) {
	if (path == NULL || packet == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(packet);
	if (header == NULL || !enp_packet_verify(packet)) {
		return ESP_ERR_INVALID_ARG;
	}

	enp_route_entry_t route;
	if (!lookup_active_route(path, &header->destination, &route)) {
		return ESP_ERR_NOT_FOUND;
	}

	return transmit_to_next_hop(path, &route, packet);
}

esp_err_t enp_routing_data_path_forward(enp_routing_data_path_t *path,
										const enp_packet_t *packet) {
	if (path == NULL || packet == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(packet);
	if (header == NULL || !enp_packet_verify(packet)) {
		return ESP_ERR_INVALID_ARG;
	}

	if (header->ttl <= 1U) {
		return ESP_ERR_INVALID_STATE;
	}

	enp_route_entry_t route;
	if (!lookup_active_route(path, &header->destination, &route)) {
		return ESP_ERR_NOT_FOUND;
	}

	enp_packet_t forwarded = *packet;
	enp_header_t *forwarded_header = enp_packet_header(&forwarded);
	if (forwarded_header == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	--forwarded_header->ttl;

	const esp_err_t seal_err =
		enp_packet_seal(&forwarded, forwarded_header->payload_length);
	if (seal_err != ESP_OK) {
		return seal_err;
	}

	return transmit_to_next_hop(path, &route, &forwarded);
}

bool enp_routing_data_path_set_route_failure_callback(
	enp_routing_data_path_t *path, enp_routing_route_failure_fn callback,
	void *context) {
	if (path == NULL) {
		return false;
	}

	path->route_failure = callback;
	path->route_failure_context = context;
	return true;
}
