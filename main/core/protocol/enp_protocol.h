/*
 * enp_network.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_protocol.h
 *
 * @brief ENP v0.2 wire protocol definitions.
 *
 * This file contains constants and enumerations that define
 * the ENP wire protocol.
 *
 * It does not define packet storage or packet manipulation.
 * Those responsibilities belong to enp_packet.h/.c.
 */

#ifndef ENP_PROTOCOL_H
#define ENP_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * Protocol Identity
 *---------------------------------------------------------*/

/**
 * @brief ENP protocol magic value.
 *
 * The magic value identifies an ENP frame.
 */
#define ENP_PROTOCOL_MAGIC 0x45534E57UL

/**
 * @brief ENP wire protocol version.
 *
 * This is the wire-format version and is independent from
 * the ENP software/library version.
 */
#define ENP_PROTOCOL_VERSION 1U

/*----------------------------------------------------------
 * Frame Limits
 *---------------------------------------------------------*/

/**
 * @brief Maximum ENP frame size.
 *
 * This includes:
 *
 * - Header
 * - Payload
 * - CRC16
 */
#define ENP_MAX_FRAME_SIZE 250U

/*----------------------------------------------------------
 * Packet Types
 *---------------------------------------------------------*/

/**
 * @brief ENP packet type.
 */
typedef enum {
	/**
	 * Invalid / uninitialized packet type.
	 */
	ENP_PACKET_INVALID = 0,

	/**
	 * Network discovery packet.
	 */
	ENP_PACKET_DISCOVERY = 1,

	/**
	 * Heartbeat packet.
	 */
	ENP_PACKET_HEARTBEAT = 2,

	/**
	 * Sensor/application telemetry packet.
	 */
	ENP_PACKET_SENSOR = 3,

	/**
	 * Acknowledgement packet.
	 */
	ENP_PACKET_ACK = 4,

	/**
	 * Routing packet.
	 */
	ENP_PACKET_ROUTE = 5,

	/**
	 * Application-defined packet.
	 */
	ENP_PACKET_APPLICATION = 6

} enp_packet_type_t;

/*----------------------------------------------------------
 * Packet Flags
 *---------------------------------------------------------*/

/**
 * @brief ENP packet flags.
 */
typedef enum {
	/**
	 * No flags.
	 */
	ENP_FLAG_NONE = 0x00,

	/**
	 * Sender requests an acknowledgement.
	 */
	ENP_FLAG_ACK_REQUIRED = 0x01,

	/**
	 * Packet is broadcast.
	 */
	ENP_FLAG_BROADCAST = 0x02,

	/**
	 * Payload is encrypted.
	 */
	ENP_FLAG_ENCRYPTED = 0x04

} enp_packet_flags_t;

/*----------------------------------------------------------
 * TTL
 *---------------------------------------------------------*/

/**
 * @brief Maximum packet hop limit.
 */
#define ENP_MAX_TTL 16U

/**
 * @brief Default packet hop limit.
 */
#define ENP_DEFAULT_TTL ENP_MAX_TTL

/*----------------------------------------------------------
 * CRC
 *---------------------------------------------------------*/

/**
 * @brief ENP frame CRC algorithm.
 *
 * ENP v0.2 uses CRC-16/CCITT-FALSE.
 *
 * Polynomial : 0x1021
 * Initial    : 0xFFFF
 * RefIn      : false
 * RefOut     : false
 * XorOut     : 0x0000
 *
 * The resulting 16-bit CRC is serialized in little-endian
 * byte order.
 */

/*----------------------------------------------------------
 * Protocol Header Sizes
 *---------------------------------------------------------*/

/**
 * @brief ENP logical address size.
 *
 * Network ID : 2 bytes
 * Node ID    : 4 bytes
 */
#define ENP_ADDRESS_SIZE 6U

/**
 * @brief ENP v0.2 frame header size.
 *
 * Magic            4
 * Version          1
 * Type             1
 * Flags            1
 * TTL              1
 * Source           6
 * Destination      6
 * Payload length   2
 * Sequence         4
 *
 * Total           26 bytes.
 */
#define ENP_HEADER_SIZE 26U

/**
 * @brief ENP CRC size.
 */
#define ENP_CRC_SIZE 2U

/**
 * @brief Maximum ENP payload size.
 */
#define ENP_MAX_PAYLOAD_SIZE                                                   \
	(ENP_MAX_FRAME_SIZE - ENP_HEADER_SIZE - ENP_CRC_SIZE)

#ifdef __cplusplus
}
#endif

#endif /* ENP_PROTOCOL_H */