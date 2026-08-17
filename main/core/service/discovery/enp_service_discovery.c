/*
 * enp_service_discovery.c
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_service_discovery.c
 *
 * @brief ENP discovery service implementation.
 */

#include "enp_service_discovery.h"

#include <string.h>

#include "esp_log.h"

#include "core/protocol/enp_protocol.h"
#include "core/service/discovery/enp_discovery.h"

/*----------------------------------------------------------
 * Logging
 *---------------------------------------------------------*/

static const char *TAG = "enp_discovery";

/*----------------------------------------------------------
 * Forward Declarations
 *---------------------------------------------------------*/

static esp_err_t enp_service_discovery_init(enp_context_t *context);

static esp_err_t
enp_service_discovery_process(enp_context_t *context,
							  const enp_packet_t *packet,
							  const enp_transport_address_t *source);

/*----------------------------------------------------------
 * Service Descriptor
 *---------------------------------------------------------*/

static const enp_service_t s_discovery_service = {
	.name = "discovery",

	.packet_type = ENP_PACKET_DISCOVERY,

	.init = enp_service_discovery_init,

	.process = enp_service_discovery_process};

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

const enp_service_t *enp_service_discovery_get(void) {
	return &s_discovery_service;
}

/*----------------------------------------------------------
 * Service Initialization
 *---------------------------------------------------------*/

static esp_err_t enp_service_discovery_init(enp_context_t *context) {
	if (context == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	ESP_LOGI(TAG, "Discovery service initialized");

	return ESP_OK;
}

/*----------------------------------------------------------
 * Discovery Transmit
 *---------------------------------------------------------*/

esp_err_t enp_service_discovery_send(enp_context_t *context) {
	if (context == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (context->transport == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	/*
	 * Build the local ENP source address.
	 */
	const enp_address_t source = {.network = context->network.id,

								  .node = context->network.local.id};

	/*
	 * Allocate the next local sequence number.
	 *
	 * ENP sequence number zero is reserved as the
	 * uninitialized value, so the context starts at 1.
	 */
	const enp_sequence_t sequence = context->network.local.next_sequence++;

	enp_packet_t packet;

	/*
	 * Initialize the ENP packet.
	 */
	enp_packet_init(&packet, ENP_PACKET_DISCOVERY, &source);

	enp_header_t *header = enp_packet_header(&packet);

	if (header == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	/*
	 * Discovery is a logical broadcast packet.
	 */
	header->destination.network = context->network.id;

	header->destination.node = ENP_NODE_BROADCAST;

	header->flags = ENP_FLAG_BROADCAST;

	header->sequence = sequence;

	/*
	 * Build discovery payload.
	 */
	enp_discovery_payload_t *payload =
		(enp_discovery_payload_t *)enp_packet_payload(&packet);

	if (payload == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	memset(payload, 0, sizeof(*payload));

	payload->role = (uint8_t)context->network.local.role;

	/*
	 * Capabilities are currently not stored in the local
	 * node runtime state.
	 *
	 * Therefore ENP v0.2 currently advertises no optional
	 * capabilities.
	 */
	payload->capabilities = 0U;

	payload->reserved = 0U;

	/*
	 * Seal the complete frame.
	 *
	 * This sets payload_length and calculates CRC16.
	 */
	esp_err_t err = enp_packet_seal(&packet, ENP_DISCOVERY_PAYLOAD_SIZE);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to seal discovery packet: %s",
				 esp_err_to_name(err));

		return err;
	}

	const size_t frame_length = enp_packet_length(&packet);

	if (frame_length == 0U) {
		return ESP_ERR_INVALID_STATE;
	}

	/*
	 * Zero-length transport address means broadcast.
	 */
	enp_transport_address_t destination;

	memset(&destination, 0, sizeof(destination));

	destination.length = 0U;

	/*
	 * Submit the complete serialized ENP frame to the
	 * transport.
	 */
	err = enp_transport_send(context->transport, &destination,
							 enp_packet_data_const(&packet), frame_length);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to send discovery: %s", esp_err_to_name(err));

		return err;
	}

	ESP_LOGD(TAG,
			 "Discovery sent: "
			 "network=%u "
			 "node=%lu "
			 "sequence=%lu "
			 "length=%u",
			 (unsigned)source.network, (unsigned long)source.node,
			 (unsigned long)sequence, (unsigned)frame_length);

	return ESP_OK;
}

/*----------------------------------------------------------
 * Discovery Receive
 *---------------------------------------------------------*/

static esp_err_t
enp_service_discovery_process(enp_context_t *context,
							  const enp_packet_t *packet,
							  const enp_transport_address_t *source) {
	if ((context == NULL) || (packet == NULL) || (source == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	const enp_header_t *header = enp_packet_header_const(packet);

	if (header == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (header->payload_length != ENP_DISCOVERY_PAYLOAD_SIZE) {
		ESP_LOGW(TAG, "Invalid discovery payload length: %u",
				 (unsigned)header->payload_length);

		return ESP_ERR_INVALID_SIZE;
	}

	const enp_discovery_payload_t *payload =
		(const enp_discovery_payload_t *)enp_packet_payload_const(packet);

	if (payload == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	/*
	 * Reserved field must be zero in ENP v0.2.
	 */
	if (payload->reserved != 0U) {
		ESP_LOGW(TAG, "Invalid discovery reserved field");

		return ESP_ERR_INVALID_ARG;
	}

	/*
	 * A broadcast logical source cannot represent an
	 * individual neighbor.
	 */
	if (enp_address_is_broadcast(&header->source)) {
		ESP_LOGW(TAG, "Ignoring discovery from broadcast source");

		return ESP_ERR_INVALID_ARG;
	}

	const uint32_t now_ms = enp_context_time_ms(context);

	/*
	 * RSSI is not currently exposed by the generic transport
	 * callback. Zero means unavailable.
	 */
	const int8_t rssi = 0;

	const esp_err_t err = enp_neighbor_update(
		&context->neighbors, &header->source, source, (enp_role_t)payload->role,
		payload->capabilities, header->sequence, rssi, now_ms);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to update neighbor: %s", esp_err_to_name(err));

		return err;
	}

	ESP_LOGI(TAG,
			 "Neighbor discovered: "
			 "network=%u "
			 "node=%lu "
			 "role=%u "
			 "capabilities=0x%04X",
			 (unsigned)header->source.network,
			 (unsigned long)header->source.node, (unsigned)payload->role,
			 (unsigned)payload->capabilities);

	return ESP_OK;
}