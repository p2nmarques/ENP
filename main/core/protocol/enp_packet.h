/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_packet.h
  *
  * @brief ENP generic packet and frame API.
  *
  * This module defines the ENP frame representation and
  * provides the API used to create, inspect, seal and verify
  * ENP packets.
  *
  * The packet subsystem is transport-independent and contains
  * no knowledge of discovery, routing, sensors or other
  * protocol services.
  */

 #ifndef ENP_PACKET_H
 #define ENP_PACKET_H

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #include "/core/enp_address.h"
 #include "enp_protocol.h"
 #include "/core/enp_types.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Packet Limits
  *---------------------------------------------------------*/

 /**
  * @brief Maximum ENP frame size in bytes.
  *
  * The value includes:
  *
  * - Header
  * - Payload
  * - CRC16
  */
 #define ENP_MAX_PACKET_SIZE       250U

 /*----------------------------------------------------------
  * Frame Header
  *---------------------------------------------------------*/

 /**
  * @brief ENP frame header.
  *
  * The header is part of the ENP v0.2 wire format.
  *
  * Header size: 26 bytes.
  *
  * Multi-byte fields use little-endian byte order.
  */
 typedef struct __attribute__((packed))
 {
     /**
      * ENP protocol magic value.
      */
     uint32_t magic;

     /**
      * ENP protocol version.
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
      * Source ENP address.
      */
     enp_address_t source;

     /**
      * Destination ENP address.
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
  * Generic Packet
  *---------------------------------------------------------*/

 /**
  * @brief ENP packet storage.
  *
  * The buffer contains the complete serialized ENP frame:
  *
  *     +-------------------+
  *     | Header            |
  *     +-------------------+
  *     | Payload           |
  *     +-------------------+
  *     | CRC16             |
  *     +-------------------+
  *
  * The internal frame layout is implemented exclusively by
  * enp_packet.c.
  */
 typedef struct
 {
     uint8_t buffer[ENP_MAX_PACKET_SIZE];

 } enp_packet_t;

 /*----------------------------------------------------------
  * Packet Lifecycle
  *---------------------------------------------------------*/

 /**
  * @brief Initialize an ENP packet.
  *
  * Initializes the protocol fields and source address.
  *
  * The destination address, sequence number and payload
  * remain available for the caller to configure.
  *
  * @param packet Packet to initialize.
  * @param type Packet type.
  * @param source Source ENP address.
  */
 void enp_packet_init(
         enp_packet_t *packet,
         enp_packet_type_t type,
         const enp_address_t *source);

 /*----------------------------------------------------------
  * Packet Data Access
  *---------------------------------------------------------*/

 /**
  * @brief Get writable packet data.
  *
  * @param packet Packet.
  *
  * @return Pointer to the complete frame buffer.
  * @return NULL if packet is NULL.
  */
 void *enp_packet_data(
         enp_packet_t *packet);

 /**
  * @brief Get read-only packet data.
  *
  * @param packet Packet.
  *
  * @return Pointer to the complete frame buffer.
  * @return NULL if packet is NULL.
  */
 const void *enp_packet_data_const(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Header Access
  *---------------------------------------------------------*/

 /**
  * @brief Get the packet header.
  *
  * @param packet Packet.
  *
  * @return Pointer to the packet header.
  * @return NULL if packet is NULL.
  */
 enp_header_t *enp_packet_header(
         enp_packet_t *packet);

 /**
  * @brief Get the packet header as read-only.
  *
  * @param packet Packet.
  *
  * @return Pointer to the packet header.
  * @return NULL if packet is NULL.
  */
 const enp_header_t *enp_packet_header_const(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Payload Access
  *---------------------------------------------------------*/

 /**
  * @brief Get writable packet payload.
  *
  * The returned pointer points immediately after the header.
  *
  * @param packet Packet.
  *
  * @return Pointer to the payload.
  * @return NULL if packet is NULL.
  */
 void *enp_packet_payload(
         enp_packet_t *packet);

 /**
  * @brief Get read-only packet payload.
  *
  * The returned pointer points immediately after the header.
  *
  * @param packet Packet.
  *
  * @return Pointer to the payload.
  * @return NULL if packet is NULL.
  */
 const void *enp_packet_payload_const(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Packet Information
  *---------------------------------------------------------*/

 /**
  * @brief Return the complete serialized packet length.
  *
  * The returned length includes the header, payload and CRC16.
  *
  * @param packet Packet.
  *
  * @return Packet length in bytes.
  * @return Zero if packet is NULL or contains an invalid
  *         payload length.
  */
 size_t enp_packet_length(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Packet Integrity
  *---------------------------------------------------------*/

 /**
  * @brief Seal an ENP packet.
  *
  * Sets the payload length and calculates the CRC16 over the
  * header and payload.
  *
  * @param packet Packet.
  * @param payload_length Payload length in bytes.
  *
  * @return ESP_OK on success.
  * @return ESP_ERR_INVALID_ARG for an invalid packet.
  * @return ESP_ERR_INVALID_SIZE if the resulting frame exceeds
  *         ENP_MAX_PACKET_SIZE.
  */
 esp_err_t enp_packet_seal(
         enp_packet_t *packet,
         uint16_t payload_length);

 /**
  * @brief Verify an ENP packet.
  *
  * Verifies:
  *
  * - Frame length
  * - Protocol magic
  * - Protocol version
  * - Packet type
  * - TTL
  * - CRC16
  *
  * @param packet Packet.
  *
  * @return true if the packet is valid.
  * @return false otherwise.
  */
 bool enp_packet_verify(
         const enp_packet_t *packet);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_PACKET_H */