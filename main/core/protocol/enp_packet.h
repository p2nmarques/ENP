/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_packet.h
 *
 * @brief ENP v0.2 frame and packet API.
 *
 * ENP packets are represented as complete frames:
 *
 *     +-------------------+
 *     | Header            |
 *     +-------------------+
 *     | Payload           |
 *     +-------------------+
 *     | CRC16             |
 *     +-------------------+
 *
 * The packet subsystem is transport-independent.
 */

#ifndef ENP_PACKET_H
#define ENP_PACKET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "core/enp_address.h"
#include "core/enp_types.h"
#include "enp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * ENP Frame Header
 *---------------------------------------------------------*/

/**
 * @brief ENP v0.2 frame header.
 *
 * Serialized size: 26 bytes.
 *
 * Multi-byte fields use little-endian byte order.
 */
typedef struct __attribute__((packed)) {
	/**
	 * ENP protocol magic value.
	 */
	uint32_t magic;

	/**
	 * ENP wire protocol version.
	 */
	uint8_t version;

	/**
	 * ENP packet type.
	 */
	uint8_t type;

	/**
	 * ENP packet flags.
	 */
	uint8_t flags;

	/**
	 * Remaining hop limit.
	 */
	uint8_t ttl;

	/**
	 * Source ENP logical address.
	 */
	enp_address_t source;

	/**
	 * Destination ENP logical address.
	 */
	enp_address_t destination;

	/**
	 * Payload length in bytes.
	 */
	uint16_t payload_length;

	/**
	 * Packet sequence number.
	 */
	enp_sequence_t sequence;

} enp_header_t;

/*----------------------------------------------------------
 * ENP Packet
 *---------------------------------------------------------*/

/**
 * @brief ENP packet storage.
 *
 * The buffer contains a complete serialized ENP frame.
 *
 * Maximum frame size is defined by ENP_PROTOCOL_H.
 */
typedef struct {
	uint8_t buffer[ENP_MAX_FRAME_SIZE];

} enp_packet_t;

/*----------------------------------------------------------
 * Packet Lifecycle
 *---------------------------------------------------------*/

/**
 * @brief Initialize an ENP packet.
 *
 * Initializes the protocol fields and source address.
 *
 * The destination, sequence number and payload are left
 * available for the caller to configure.
 *
 * @param packet Packet to initialize.
 * @param type Packet type.
 * @param source Source ENP address.
 */
void enp_packet_init(enp_packet_t *packet, enp_packet_type_t type,
					 const enp_address_t *source);

/*----------------------------------------------------------
 * Raw Frame Access
 *---------------------------------------------------------*/

/**
 * @brief Get writable frame data.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the frame buffer.
 * @return NULL if packet is NULL.
 */
void *enp_packet_data(enp_packet_t *packet);

/**
 * @brief Get read-only frame data.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the frame buffer.
 * @return NULL if packet is NULL.
 */
const void *enp_packet_data_const(const enp_packet_t *packet);

/*----------------------------------------------------------
 * Header Access
 *---------------------------------------------------------*/

/**
 * @brief Get writable packet header.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the packet header.
 * @return NULL if packet is NULL.
 */
enp_header_t *enp_packet_header(enp_packet_t *packet);

/**
 * @brief Get read-only packet header.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the packet header.
 * @return NULL if packet is NULL.
 */
const enp_header_t *enp_packet_header_const(const enp_packet_t *packet);

/*----------------------------------------------------------
 * Payload Access
 *---------------------------------------------------------*/

/**
 * @brief Get writable packet payload.
 *
 * The returned pointer points immediately after the header.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the payload.
 * @return NULL if packet is NULL.
 */
void *enp_packet_payload(enp_packet_t *packet);

/**
 * @brief Get read-only packet payload.
 *
 * The returned pointer points immediately after the header.
 *
 * @param packet ENP packet.
 *
 * @return Pointer to the payload.
 * @return NULL if packet is NULL.
 */
const void *enp_packet_payload_const(const enp_packet_t *packet);

/*----------------------------------------------------------
 * Packet Information
 *---------------------------------------------------------*/

/**
 * @brief Return the complete serialized frame length.
 *
 * The returned length includes:
 *
 * - Header
 * - Payload
 * - CRC16
 *
 * @param packet ENP packet.
 *
 * @return Frame length in bytes.
 * @return Zero if packet is NULL or has an invalid payload
 *         length.
 */
size_t enp_packet_length(const enp_packet_t *packet);

/*----------------------------------------------------------
 * Packet Integrity
 *---------------------------------------------------------*/

/**
 * @brief Seal an ENP packet.
 *
 * Sets the payload length and calculates the CRC16 over the
 * header and payload.
 *
 * @param packet ENP packet.
 * @param payload_length Payload length in bytes.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 * @return ESP_ERR_INVALID_SIZE if the resulting frame exceeds
 *         ENP_MAX_FRAME_SIZE.
 * @return ESP_ERR_INVALID_STATE if the packet has not been
 *         initialized correctly.
 */
esp_err_t enp_packet_seal(enp_packet_t *packet, uint16_t payload_length);

/**
 * @brief Verify an ENP packet.
 *
 * Verifies:
 *
 * - Protocol magic
 * - Protocol version
 * - Packet type
 * - TTL
 * - Payload length
 * - CRC16
 *
 * @param packet ENP packet.
 *
 * @return true if the packet is valid.
 * @return false otherwise.
 */
bool enp_packet_verify(const enp_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif /* ENP_PACKET_H */