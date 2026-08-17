/*
 * packets.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_packet.c
 *
 * @brief ENP v0.2 frame and packet implementation.
 */

#include "enp_packet.h"

#include <string.h>

#include "enp_crc16.h"

/*----------------------------------------------------------
 * Private Constants
 *---------------------------------------------------------*/

#define ENP_PACKET_OVERHEAD ((size_t)ENP_HEADER_SIZE + (size_t)ENP_CRC_SIZE)

#define ENP_PACKET_MIN_SIZE ENP_PACKET_OVERHEAD

/*----------------------------------------------------------
 * Compile-Time Validation
 *---------------------------------------------------------*/

_Static_assert(sizeof(enp_address_t) == ENP_ADDRESS_SIZE,
			   "Unexpected ENP address size");

_Static_assert(sizeof(enp_header_t) == ENP_HEADER_SIZE,
			   "Unexpected ENP header size");

_Static_assert(sizeof(enp_sequence_t) == sizeof(uint32_t),
			   "Unexpected ENP sequence size");

_Static_assert(sizeof(enp_node_id_t) == sizeof(uint32_t),
			   "Unexpected ENP node ID size");

_Static_assert(sizeof(enp_network_id_t) == sizeof(uint16_t),
			   "Unexpected ENP network ID size");

_Static_assert(ENP_HEADER_SIZE + ENP_CRC_SIZE < ENP_MAX_FRAME_SIZE,
			   "ENP frame overhead exceeds maximum frame size");

/*----------------------------------------------------------
 * Private Functions
 *---------------------------------------------------------*/

static void enp_write_u16_le(uint8_t *destination, uint16_t value);

static uint16_t enp_read_u16_le(const uint8_t *source);

/*----------------------------------------------------------
 * Packet Initialization
 *---------------------------------------------------------*/

void enp_packet_init(enp_packet_t *packet, enp_packet_type_t type,
					 const enp_address_t *source) {
	if (packet == NULL) {
		return;
	}

	memset(packet, 0, sizeof(*packet));

	enp_header_t *header = enp_packet_header(packet);

	header->magic = ENP_PROTOCOL_MAGIC;

	header->version = ENP_PROTOCOL_VERSION;

	header->type = (uint8_t)type;

	header->flags = ENP_FLAG_NONE;

	header->ttl = ENP_DEFAULT_TTL;

	if (source != NULL) {
		header->source = *source;
	}
}

/*----------------------------------------------------------
 * Raw Frame Access
 *---------------------------------------------------------*/

void *enp_packet_data(enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return packet->buffer;
}

const void *enp_packet_data_const(const enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return packet->buffer;
}

/*----------------------------------------------------------
 * Header Access
 *---------------------------------------------------------*/

enp_header_t *enp_packet_header(enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return (enp_header_t *)packet->buffer;
}

const enp_header_t *enp_packet_header_const(const enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return (const enp_header_t *)packet->buffer;
}

/*----------------------------------------------------------
 * Payload Access
 *---------------------------------------------------------*/

void *enp_packet_payload(enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return packet->buffer + ENP_HEADER_SIZE;
}

const void *enp_packet_payload_const(const enp_packet_t *packet) {
	if (packet == NULL) {
		return NULL;
	}

	return packet->buffer + ENP_HEADER_SIZE;
}

/*----------------------------------------------------------
 * Packet Information
 *---------------------------------------------------------*/

size_t enp_packet_length(const enp_packet_t *packet) {
	if (packet == NULL) {
		return 0U;
	}

	const enp_header_t *header = enp_packet_header_const(packet);

	if (header == NULL) {
		return 0U;
	}

	const size_t payload_length = (size_t)header->payload_length;

	if (payload_length > ENP_MAX_PAYLOAD_SIZE) {
		return 0U;
	}

	return ENP_PACKET_OVERHEAD + payload_length;
}

/*----------------------------------------------------------
 * Packet Sealing
 *---------------------------------------------------------*/

esp_err_t enp_packet_seal(enp_packet_t *packet, uint16_t payload_length) {
	if (packet == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if ((size_t)payload_length > (size_t)ENP_MAX_PAYLOAD_SIZE) {
		return ESP_ERR_INVALID_SIZE;
	}

	enp_header_t *header = enp_packet_header(packet);

	if (header == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	/*
	 * The packet must have been initialized through
	 * enp_packet_init().
	 */
	if (header->magic != ENP_PROTOCOL_MAGIC) {
		return ESP_ERR_INVALID_STATE;
	}

	if (header->version != ENP_PROTOCOL_VERSION) {
		return ESP_ERR_INVALID_STATE;
	}

	if ((header->type == ENP_PACKET_INVALID) ||
		(header->type > ENP_PACKET_APPLICATION)) {
		return ESP_ERR_INVALID_ARG;
	}

	if (header->ttl > ENP_MAX_TTL) {
		return ESP_ERR_INVALID_ARG;
	}

	header->payload_length = payload_length;

	const size_t frame_length = ENP_PACKET_OVERHEAD + (size_t)payload_length;

	uint8_t *crc_location = packet->buffer + frame_length - ENP_CRC_SIZE;

	const uint16_t crc = enp_crc16(packet->buffer, frame_length - ENP_CRC_SIZE);

	enp_write_u16_le(crc_location, crc);

	return ESP_OK;
}

/*----------------------------------------------------------
 * Packet Verification
 *---------------------------------------------------------*/

bool enp_packet_verify(const enp_packet_t *packet) {
	if (packet == NULL) {
		return false;
	}

	const enp_header_t *header = enp_packet_header_const(packet);

	if (header == NULL) {
		return false;
	}

	/*
	 * Protocol identity.
	 */
	if (header->magic != ENP_PROTOCOL_MAGIC) {
		return false;
	}

	if (header->version != ENP_PROTOCOL_VERSION) {
		return false;
	}

	/*
	 * Packet type.
	 */
	if ((header->type == ENP_PACKET_INVALID) ||
		(header->type > ENP_PACKET_APPLICATION)) {
		return false;
	}

	/*
	 * TTL.
	 */
	if (header->ttl > ENP_MAX_TTL) {
		return false;
	}

	/*
	 * Payload length.
	 */
	if ((size_t)header->payload_length > (size_t)ENP_MAX_PAYLOAD_SIZE) {
		return false;
	}

	const size_t frame_length =
		ENP_PACKET_OVERHEAD + (size_t)header->payload_length;

	if ((frame_length < ENP_PACKET_MIN_SIZE) ||
		(frame_length > ENP_MAX_FRAME_SIZE)) {
		return false;
	}

	/*
	 * CRC.
	 */
	const uint8_t *crc_location = packet->buffer + frame_length - ENP_CRC_SIZE;

	const uint16_t received_crc = enp_read_u16_le(crc_location);

	const uint16_t calculated_crc =
		enp_crc16(packet->buffer, frame_length - ENP_CRC_SIZE);

	return received_crc == calculated_crc;
}

/*----------------------------------------------------------
 * Little-Endian Helpers
 *---------------------------------------------------------*/

static void enp_write_u16_le(uint8_t *destination, uint16_t value) {
	destination[0] = (uint8_t)(value & 0xFFU);

	destination[1] = (uint8_t)((value >> 8U) & 0xFFU);
}

static uint16_t enp_read_u16_le(const uint8_t *source) {
	return (uint16_t)((uint16_t)source[0] | ((uint16_t)source[1] << 8U));
}