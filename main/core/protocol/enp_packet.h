/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_packet.h
  *
  * @brief ENP generic packet representation.
  */

 #ifndef ENP_PACKET_H
 #define ENP_PACKET_H

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 #include "esp_err.h"

 #include "enp_protocol.h"
 #include "/core/enp_types.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 /*----------------------------------------------------------
  * Common Packet Header
  *---------------------------------------------------------*/

 /**
  * @brief Common ENP packet header.
  *
  * Every ENP packet begins with this header.
  */
 typedef struct __attribute__((packed))
 {
     uint32_t magic;

     uint8_t version;

     uint8_t type;

     uint8_t flags;

     uint8_t ttl;

     enp_node_id_t source;

     enp_node_id_t destination;

     enp_sequence_t sequence;

     uint16_t payload_length;

 } enp_header_t;

 /*----------------------------------------------------------
  * Generic Packet
  *---------------------------------------------------------*/

 /**
  * @brief Generic ENP packet.
  *
  * The packet owns a contiguous byte buffer containing:
  *
  *   Header
  *   Payload
  *   CRC16
  */
  typedef struct
  {
      uint8_t data[ENP_MAX_PACKET_SIZE];

  } enp_packet_t;

 /*----------------------------------------------------------
  * Packet Accessors
  *---------------------------------------------------------*/

 /**
  * @brief Returns the packet header.
  */
 enp_header_t *enp_packet_header(
         enp_packet_t *packet);

 /**
  * @brief Returns the packet header (const).
  */
 const enp_header_t *enp_packet_header_const(
         const enp_packet_t *packet);

 /**
  * @brief Returns the payload.
  */
 void *enp_packet_payload(
         enp_packet_t *packet);

 /**
  * @brief Returns the payload (const).
  */
 const void *enp_packet_payload_const(
         const enp_packet_t *packet);

 /**
  * @brief Returns a pointer to the packet CRC.
  */
 uint16_t *enp_packet_crc(
         enp_packet_t *packet);

 /**
  * @brief Returns a pointer to the packet CRC (const).
  */
 const uint16_t *enp_packet_crc_const(
         const enp_packet_t *packet);

 /**
  * @brief Returns the current packet size.
  */
 size_t enp_packet_size(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Packet Lifecycle
  *---------------------------------------------------------*/

 /**
  * @brief Initializes a packet.
  */
 void enp_packet_init(
         enp_packet_t *packet);

 /**
  * @brief Finalizes a packet.
  *
  * Calculates:
  *  - packet length
  *  - CRC16
  */
 esp_err_t enp_packet_finalize(
         enp_packet_t *packet);

 /**
  * @brief Verifies a received packet.
  */
 bool enp_packet_verify(
         const enp_packet_t *packet);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_PACKET_H */