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

static bool route_destination_equal(enp_route_destination_t lhs,
                                    enp_route_destination_t rhs) {
    return lhs.network_id == rhs.network_id && lhs.node_id == rhs.node_id;
}

/* Record transient evidence for a forwarded packet. */
static bool record_forward_evidence(
    enp_routing_data_path_t *path, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop, enp_route_destination_t upstream) {
    if (path == NULL) return false;
    portENTER_CRITICAL(&path->forward_evidence_lock);
    size_t free_index = ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE;
    for (size_t i = 0U; i < ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE; ++i) {
        if (!path->forward_evidence[i].valid) {
            if (free_index == ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE) free_index = i;
            continue;
        }
        if (route_destination_equal(path->forward_evidence[i].destination, destination) &&
            route_destination_equal(path->forward_evidence[i].failed_next_hop, failed_next_hop) &&
            route_destination_equal(path->forward_evidence[i].upstream, upstream)) {
            portEXIT_CRITICAL(&path->forward_evidence_lock);
            return true;
        }
    }
    if (free_index == ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE) {
        portEXIT_CRITICAL(&path->forward_evidence_lock);
        return false;
    }
    path->forward_evidence[free_index].destination = destination;
    path->forward_evidence[free_index].failed_next_hop = failed_next_hop;
    path->forward_evidence[free_index].upstream = upstream;
    path->forward_evidence[free_index].valid = true;
    portEXIT_CRITICAL(&path->forward_evidence_lock);
    return true;
}

/* Retrieve and consume unambiguous matching upstream evidence. */
static bool take_forward_evidence(
    enp_routing_data_path_t *path, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop, enp_route_destination_t *upstream) {
    if (path == NULL || upstream == NULL) return false;
    bool found = false, ambiguous = false;
    size_t found_index = ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE;
    enp_route_destination_t candidate = {0};
    portENTER_CRITICAL(&path->forward_evidence_lock);
    for (size_t i = 0U; i < ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE; ++i) {
        if (!path->forward_evidence[i].valid ||
            !route_destination_equal(path->forward_evidence[i].destination, destination) ||
            !route_destination_equal(path->forward_evidence[i].failed_next_hop, failed_next_hop)) continue;
        if (!found) {
            candidate = path->forward_evidence[i].upstream;
            found_index = i;
            found = true;
        } else if (!route_destination_equal(candidate, path->forward_evidence[i].upstream)) {
            ambiguous = true;
            break;
        }
    }
    if (found && !ambiguous) {
        path->forward_evidence[found_index].valid = false;
        *upstream = candidate;
    }
    portEXIT_CRITICAL(&path->forward_evidence_lock);
    return found && !ambiguous;
}

static void discard_forward_evidence(
    enp_routing_data_path_t *path, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop, enp_route_destination_t upstream) {
    if (path == NULL) {
        return;
    }

    portENTER_CRITICAL(&path->forward_evidence_lock);
    for (size_t i = 0U; i < ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE; ++i) {
        if (path->forward_evidence[i].valid &&
            route_destination_equal(path->forward_evidence[i].destination, destination) &&
            route_destination_equal(path->forward_evidence[i].failed_next_hop, failed_next_hop) &&
            route_destination_equal(path->forward_evidence[i].upstream, upstream)) {
            path->forward_evidence[i].valid = false;
            break;
        }
    }
    portEXIT_CRITICAL(&path->forward_evidence_lock);
}

static void notify_route_failure(
    enp_routing_data_path_t *path, enp_route_destination_t destination,
    enp_route_destination_t failed_next_hop, bool have_upstream,
    enp_route_destination_t upstream) {
    if (path == NULL) return;
    if (path->route_failure_ex != NULL) {
        path->route_failure_ex(path->route_failure_ex_context, destination,
                               failed_next_hop, have_upstream ? upstream
                                                            : (enp_route_destination_t){0});
        return;
    }
    if (path->route_failure != NULL) {
        path->route_failure(path->route_failure_context, destination, failed_next_hop);
    }
}

static void invalidate_routes_for_transport_failure(
	enp_routing_data_path_t *path, const enp_transport_address_t *destination,
	bool should_notify_route_failure) {
	if (path == NULL || path->routes == NULL || destination == NULL ||
		path->resolve_transport == NULL) {
		return;
	}

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
				should_notify_route_failure) {
				enp_route_destination_t upstream = {0};
				const bool have_upstream = take_forward_evidence(
						path, entry->destination, failed_next_hop, &upstream);
				notify_route_failure(path, entry->destination, failed_next_hop,
									 have_upstream, upstream);
			}
		}
	}
}

static void
transport_send_result_callback(const enp_transport_address_t *destination,
							   esp_err_t result, void *context) {
	enp_routing_data_path_t *path = (enp_routing_data_path_t *)context;
	if (result == ESP_OK) {
		return;
	}
	invalidate_routes_for_transport_failure(path, destination, true);
}

static void transport_send_result_callback_ex(
	const enp_transport_address_t *destination, esp_err_t result,
	enp_transport_correlation_id_t correlation_id, void *context) {
	enp_routing_data_path_t *path = (enp_routing_data_path_t *)context;

	if (result != ESP_OK) {
		invalidate_routes_for_transport_failure(
			path, destination,
			correlation_id == ENP_TRANSPORT_INVALID_CORRELATION_ID);
	}

	if (path != NULL && path->correlated_failure != NULL &&
		correlation_id != ENP_TRANSPORT_INVALID_CORRELATION_ID) {
		path->correlated_failure(path->correlated_failure_context, destination,
							 result, correlation_id);
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

/*
 * Convert failure to resolve an already-selected next hop into the existing
 * E5C route-failure boundary. The route identity guard prevents an older
 * submission from invalidating a replacement route installed concurrently.
 */
static void report_next_hop_admission_failure(
	enp_routing_data_path_t *path, const enp_route_entry_t *route) {
	if (path == NULL || route == NULL || path->routes == NULL) {
		return;
	}

	const enp_route_destination_t destination = route->destination;
	enp_route_entry_t *current =
		enp_route_table_lookup(path->routes, destination);
	if (current == NULL || current->state != ENP_ROUTE_STATE_ACTIVE ||
		current->next_hop.network_id != route->next_hop.network_id ||
		current->next_hop.node_id != route->next_hop.node_id) {
		return;
	}

	const enp_route_destination_t failed_next_hop = current->next_hop;
	if (enp_route_table_invalidate(path->routes, destination)) {
		enp_route_destination_t upstream = {0};
		const bool have_upstream = take_forward_evidence(
			path, destination, failed_next_hop, &upstream);
		notify_route_failure(path, destination, failed_next_hop, have_upstream, upstream);
	}
}

static esp_err_t transmit_to_next_hop_correlated(
	const enp_routing_data_path_t *path, const enp_route_entry_t *route,
	const enp_packet_t *packet, enp_transport_correlation_id_t correlation_id) {
	if (path == NULL || route == NULL || packet == NULL ||
		path->transport == NULL || path->resolve_transport == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	enp_transport_address_t transport_address = {0};
	if (!path->resolve_transport(path->resolve_context, route->next_hop,
								 &transport_address)) {
		report_next_hop_admission_failure((enp_routing_data_path_t *)path,
										 route);
		return ESP_ERR_NOT_FOUND;
	}

	esp_err_t send_err = enp_transport_send_ex(
		path->transport, &transport_address, enp_packet_data_const(packet),
		enp_packet_length(packet), correlation_id);
	if (send_err != ESP_OK) {
		const enp_header_t *header = enp_packet_header_const(packet);
		if (header != NULL) {
			discard_forward_evidence(
				(enp_routing_data_path_t *)path, route->destination,
				route->next_hop,
				(enp_route_destination_t){.network_id = header->source.network,
								 .node_id = header->source.node});
		}
	}
	return send_err;
}

static esp_err_t transmit_to_next_hop(
	const enp_routing_data_path_t *path, const enp_route_entry_t *route,
	const enp_packet_t *packet) {
	if (path == NULL || route == NULL || packet == NULL ||
		path->transport == NULL || path->resolve_transport == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	enp_transport_address_t transport_address = {0};
	if (!path->resolve_transport(path->resolve_context, route->next_hop,
								 &transport_address)) {
		report_next_hop_admission_failure((enp_routing_data_path_t *)path,
										 route);
		return ESP_ERR_NOT_FOUND;
	}

	esp_err_t send_err = enp_transport_send(
		path->transport, &transport_address, enp_packet_data_const(packet),
		enp_packet_length(packet));
	if (send_err != ESP_OK) {
		const enp_header_t *header = enp_packet_header_const(packet);
		if (header != NULL) {
			discard_forward_evidence(
				(enp_routing_data_path_t *)path, route->destination,
				route->next_hop,
				(enp_route_destination_t){.network_id = header->source.network,
								 .node_id = header->source.node});
		}
	}
	return send_err;
}

bool enp_routing_data_path_init(
	enp_routing_data_path_t *path, enp_route_table_t *routes,
	enp_transport_t *transport,
	enp_routing_resolve_transport_fn resolve_transport, void *resolve_context) {
	if (path == NULL || routes == NULL || transport == NULL ||
		transport->send == NULL || resolve_transport == NULL) {
		return false;
	}

	path->routes = routes;
	path->transport = transport;
	path->resolve_transport = resolve_transport;
	path->resolve_context = resolve_context;
	path->forward_evidence_lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
	for (size_t i = 0U; i < ENP_ROUTING_DATA_PATH_MAX_FORWARD_EVIDENCE; ++i) {
		path->forward_evidence[i].valid = false;
	}

	const esp_err_t callback_err =
		(transport->set_send_result_callback_ex != NULL &&
		 transport->send_ex != NULL)
			? enp_transport_set_send_result_callback_ex(
				  transport, transport_send_result_callback_ex, path)
			: enp_transport_set_send_result_callback(
				  transport, transport_send_result_callback, path);

	if (callback_err != ESP_OK) {
		path->routes = NULL;
		path->transport = NULL;
		path->resolve_transport = NULL;
		path->resolve_context = NULL;
		return false;
	}

	return true;
}

bool enp_routing_data_path_get_next_hop(
	const enp_routing_data_path_t *path, const enp_address_t *destination,
	enp_route_destination_t *next_hop) {
	if (next_hop == NULL) {
		return false;
	}

	enp_route_entry_t route;
	if (!lookup_active_route(path, destination, &route)) {
		return false;
	}

	*next_hop = route.next_hop;
	return true;
}

esp_err_t enp_routing_data_path_submit_correlated(
	enp_routing_data_path_t *path, const enp_packet_t *packet,
	enp_transport_correlation_id_t correlation_id) {
	if (path == NULL || packet == NULL ||
		correlation_id == ENP_TRANSPORT_INVALID_CORRELATION_ID) {
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

	return transmit_to_next_hop_correlated(path, &route, packet, correlation_id);
}

bool enp_routing_data_path_set_correlated_failure_callback(
	enp_routing_data_path_t *path, enp_routing_correlated_failure_fn callback,
	void *context) {
	if (path == NULL) {
		return false;
	}

	path->correlated_failure = callback;
	path->correlated_failure_context = context;
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

	(void)record_forward_evidence(
		path, (enp_route_destination_t){
			.network_id = header->destination.network,
			.node_id = header->destination.node},
		route.next_hop,
		(enp_route_destination_t){
			.network_id = header->source.network,
			.node_id = header->source.node});

	enp_packet_t forwarded = *packet;
	enp_header_t *forwarded_header = enp_packet_header(&forwarded);
	if (forwarded_header == NULL) {
		discard_forward_evidence(
			path, route.destination, route.next_hop,
			(enp_route_destination_t){.network_id = header->source.network,
								 .node_id = header->source.node});
		return ESP_ERR_INVALID_STATE;
	}

	--forwarded_header->ttl;

	const esp_err_t seal_err =
		enp_packet_seal(&forwarded, forwarded_header->payload_length);
	if (seal_err != ESP_OK) {
		discard_forward_evidence(
			path, route.destination, route.next_hop,
			(enp_route_destination_t){.network_id = header->source.network,
								 .node_id = header->source.node});
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

bool enp_routing_data_path_set_route_failure_callback_ex(
	enp_routing_data_path_t *path, enp_routing_route_failure_ex_fn callback,
	void *context) {
	if (path == NULL) {
		return false;
	}

	path->route_failure_ex = callback;
	path->route_failure_ex_context = context;
	return true;
}
