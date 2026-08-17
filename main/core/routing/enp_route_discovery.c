/*
 * enp_rerr_processor.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

/*
 * enp_route_discovery.c
 *
 * ENP v0.2 Route Discovery State Machine — R4-A.1
 */

#include "enp_route_discovery.h"

#include <string.h>

static bool destination_valid(enp_discovery_destination_t destination) {
	return destination.network_id != 0U && destination.node_id != 0U;
}

/* RFC-style uint32 serial-number comparison. */
static bool sequence_is_newer_or_equal(uint32_t received, uint32_t requested) {
	uint32_t difference = received - requested;

	return difference == 0U || difference < 0x80000000UL;
}

/* Wrap-safe deadline test: true when now is at or after deadline. */
static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms) {
	return (int32_t)(now_ms - deadline_ms) >= 0;
}

bool enp_route_discovery_init(enp_route_discovery_t *discovery) {
	if (discovery == NULL) {
		return false;
	}

	memset(discovery, 0, sizeof(*discovery));
	discovery->state = ENP_DISCOVERY_STATE_IDLE;

	return true;
}

bool enp_route_discovery_start(enp_route_discovery_t *discovery,
							   enp_discovery_destination_t destination,
							   uint32_t route_request_id,
							   uint32_t destination_sequence, uint8_t ttl,
							   uint32_t now_ms) {
	if (discovery == NULL) {
		return false;
	}

	if (!destination_valid(destination) || route_request_id == 0U ||
		ttl == 0U) {
		return false;
	}

	if (discovery->state == ENP_DISCOVERY_STATE_REQUESTING) {
		return false;
	}

	discovery->state = ENP_DISCOVERY_STATE_REQUESTING;
	discovery->destination = destination;
	discovery->route_request_id = route_request_id;
	discovery->destination_sequence = destination_sequence;
	discovery->ttl = ttl;
	discovery->retry_count = 0U;
	discovery->started_at_ms = now_ms;
	discovery->deadline_ms = now_ms + ENP_DISCOVERY_TIMEOUT_MS;

	return true;
}

bool enp_route_discovery_on_rrep(enp_route_discovery_t *discovery,
								 enp_discovery_destination_t destination,
								 uint32_t destination_sequence) {
	if (discovery == NULL ||
		discovery->state != ENP_DISCOVERY_STATE_REQUESTING) {
		return false;
	}

	if (!destination_valid(destination)) {
		return false;
	}

	if (destination.network_id != discovery->destination.network_id ||
		destination.node_id != discovery->destination.node_id) {
		return false;
	}

	if (!sequence_is_newer_or_equal(destination_sequence,
									discovery->destination_sequence)) {
		return false;
	}

	discovery->state = ENP_DISCOVERY_STATE_COMPLETE;

	return true;
}

bool enp_route_discovery_on_timeout(enp_route_discovery_t *discovery,
									uint32_t now_ms) {
	if (discovery == NULL ||
		discovery->state != ENP_DISCOVERY_STATE_REQUESTING) {
		return false;
	}

	if (!deadline_reached(now_ms, discovery->deadline_ms)) {
		return false;
	}

	if (discovery->retry_count >= ENP_MAX_RETRIES) {
		discovery->state = ENP_DISCOVERY_STATE_FAILED;
		return false;
	}

	discovery->retry_count++;
	discovery->started_at_ms = now_ms;
	discovery->deadline_ms = now_ms + ENP_DISCOVERY_TIMEOUT_MS;

	return true;
}

bool enp_route_discovery_is_active(const enp_route_discovery_t *discovery) {
	return discovery != NULL &&
		   discovery->state == ENP_DISCOVERY_STATE_REQUESTING;
}

enp_discovery_state_t
enp_route_discovery_state(const enp_route_discovery_t *discovery) {
	if (discovery == NULL) {
		return ENP_DISCOVERY_STATE_FAILED;
	}

	return discovery->state;
}