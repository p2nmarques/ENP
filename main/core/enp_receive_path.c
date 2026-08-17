/*
 * enp_receive_path.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E4B — ENP production receive-path integration.
 */

#include "core/enp_receive_path.h"

#include <string.h>

#include "core/dispatcher/enp_dispatcher.h"
#include "core/protocol/enp_packet.h"
#include "core/protocol/enp_protocol.h"

static enp_receive_path_t *s_bound_path = NULL;

static esp_err_t
enp_receive_path_local_process(void *context, const enp_packet_t *packet,
							   const enp_transport_address_t *source) {
	(void)context;

	return enp_dispatcher_dispatch_local(packet, source);
}

esp_err_t enp_receive_path_init(enp_receive_path_t *path,
								enp_context_t *context,
								enp_routing_data_path_t *routing_path) {
	if ((path == NULL) || (context == NULL) || (routing_path == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	if (path->initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	memset(path, 0, sizeof(*path));

	esp_err_t err =
		enp_data_plane_init(&path->data_plane, context, routing_path,
							enp_receive_path_local_process);

	if (err != ESP_OK) {
		return err;
	}

	path->context = context;
	path->routing_path = routing_path;
	path->initialized = true;

	return ESP_OK;
}

esp_err_t enp_receive_path_deinit(enp_receive_path_t *path) {
	if (path == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!path->initialized) {
		return ESP_OK;
	}

	if (s_bound_path == path) {
		s_bound_path = NULL;
	}

	esp_err_t err = enp_data_plane_deinit(&path->data_plane);

	if (err != ESP_OK) {
		return err;
	}

	path->context = NULL;
	path->routing_path = NULL;
	path->initialized = false;

	return ESP_OK;
}

esp_err_t enp_receive_path_process(enp_receive_path_t *path,
								   const enp_transport_address_t *source,
								   const void *data, size_t length) {
	if ((path == NULL) || (source == NULL) || (data == NULL) ||
		(length == 0U)) {
		return ESP_ERR_INVALID_ARG;
	}

	if (!path->initialized) {
		return ESP_ERR_INVALID_STATE;
	}

	if (length > sizeof(enp_packet_t)) {
		return ESP_ERR_INVALID_SIZE;
	}

	enp_packet_t packet;
	memset(&packet, 0, sizeof(packet));
	memcpy(enp_packet_data(&packet), data, length);

	if (!enp_packet_verify(&packet)) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(&packet);

	if (header == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	if ((header->type == (uint8_t)ENP_PACKET_APPLICATION) ||
		(header->type == (uint8_t)ENP_PACKET_ACK)) {
		return enp_data_plane_process(&path->data_plane, &packet, source);
	}

	return enp_dispatcher_dispatch(&packet, source);
}

void enp_receive_path_transport_callback(const enp_transport_address_t *source,
										 const void *data, size_t length) {
	if (s_bound_path == NULL) {
		return;
	}

	(void)enp_receive_path_process(s_bound_path, source, data, length);
}

esp_err_t enp_receive_path_bind(enp_receive_path_t *path) {
	if ((path == NULL) || !path->initialized) {
		return ESP_ERR_INVALID_ARG;
	}

	if ((s_bound_path != NULL) && (s_bound_path != path)) {
		return ESP_ERR_INVALID_STATE;
	}

	s_bound_path = path;
	return ESP_OK;
}
