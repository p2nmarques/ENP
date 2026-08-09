/*
 * packets.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */


 /**
  * @file enp_packet.c
  *
  * @brief ENP generic packet implementation.
  */

 #include "enp_packet.h"

 #include <string.h>

 #include "enp_crc16.h"

 /*----------------------------------------------------------
  * Private Constants
  *---------------------------------------------------------*/

 #define ENP_HEADER_SIZE \
     ((size_t)sizeof(enp_header_t))

 #define ENP_CRC_SIZE \
     ((size_t)sizeof(uint16_t))

 #define ENP_PACKET_OVERHEAD \
     (ENP_HEADER_SIZE + ENP_CRC_SIZE)

 #define ENP_PACKET_MIN_SIZE \
     ENP_PACKET_OVERHEAD

 /*----------------------------------------------------------
  * Compile-Time Validation
  *---------------------------------------------------------*/

 _Static_assert(
         sizeof(enp_header_t) < ENP_MAX_PACKET_SIZE,
         "ENP header exceeds maximum packet size");

 _Static_assert(
         sizeof(enp_sequence_t) == sizeof(uint32_t),
         "Unexpected ENP sequence number size");

 _Static_assert(
         sizeof(enp_node_id_t) == sizeof(uint32_t),
         "Unexpected ENP node ID size");

 _Static_assert(
         sizeof(enp_network_id_t) == sizeof(uint16_t),
         "Unexpected ENP network ID size");

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static uint16_t *enp_packet_crc(
         enp_packet_t *packet);

 static const uint16_t *enp_packet_crc_const(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 void enp_packet_init(
         enp_packet_t *packet,
         enp_packet_type_t type,
         const enp_address_t *source)
 {
     if (packet == NULL)
     {
         return;
     }

     memset(packet, 0, sizeof(*packet));

     enp_header_t *header =
             enp_packet_header(packet);

     header->magic = ENP_PROTOCOL_MAGIC;

     header->version = ENP_PROTOCOL_VERSION;

     header->type = (uint8_t)type;

     header->flags = ENP_FLAG_NONE;

     header->ttl = ENP_DEFAULT_TTL;

     if (source != NULL)
     {
         header->source = *source;
     }
 }

 void *enp_packet_data(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->buffer;
 }

 const void *enp_packet_data_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->buffer;
 }

 enp_header_t *enp_packet_header(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return (enp_header_t *)packet->buffer;
 }

 const enp_header_t *enp_packet_header_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return (const enp_header_t *)packet->buffer;
 }

 void *enp_packet_payload(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->buffer + ENP_HEADER_SIZE;
 }

 const void *enp_packet_payload_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->buffer + ENP_HEADER_SIZE;
 }

 size_t enp_packet_length(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return 0U;
     }

     const enp_header_t *header =
             enp_packet_header_const(packet);

     if (header == NULL)
     {
         return 0U;
     }

     return ENP_PACKET_OVERHEAD +
            (size_t)header->payload_length;
 }

 esp_err_t enp_packet_seal(
         enp_packet_t *packet,
         uint16_t payload_length)
 {
     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     const size_t length =
             ENP_PACKET_OVERHEAD +
             (size_t)payload_length;

     if (length > ENP_MAX_PACKET_SIZE)
     {
         return ESP_ERR_INVALID_SIZE;
     }

     enp_header_t *header =
             enp_packet_header(packet);

     if (header == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     header->payload_length = payload_length;

     uint16_t *crc =
             enp_packet_crc(packet);

     if (crc == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     *crc = enp_crc16(
             enp_packet_data_const(packet),
             length - ENP_CRC_SIZE);

     return ESP_OK;
 }

 bool enp_packet_verify(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return false;
     }

     const size_t length =
             enp_packet_length(packet);

     if ((length < ENP_PACKET_MIN_SIZE) ||
         (length > ENP_MAX_PACKET_SIZE))
     {
         return false;
     }

     const enp_header_t *header =
             enp_packet_header_const(packet);

     if (header == NULL)
     {
         return false;
     }

     if (header->magic != ENP_PROTOCOL_MAGIC)
     {
         return false;
     }

     if (header->version != ENP_PROTOCOL_VERSION)
     {
         return false;
     }

     if (header->ttl > ENP_MAX_TTL)
     {
         return false;
     }

     const uint16_t calculated_crc =
             enp_crc16(
                     enp_packet_data_const(packet),
                     length - ENP_CRC_SIZE);

     const uint16_t received_crc =
             *enp_packet_crc_const(packet);

     return calculated_crc == received_crc;
 }

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static uint16_t *enp_packet_crc(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     const size_t length =
             enp_packet_length(packet);

     if ((length < ENP_PACKET_MIN_SIZE) ||
         (length > ENP_MAX_PACKET_SIZE))
     {
         return NULL;
     }

     return (uint16_t *)
     (
         packet->buffer +
         length -
         ENP_CRC_SIZE
     );
 }

 static const uint16_t *enp_packet_crc_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     const size_t length =
             enp_packet_length(packet);

     if ((length < ENP_PACKET_MIN_SIZE) ||
         (length > ENP_MAX_PACKET_SIZE))
     {
         return NULL;
     }

     return (const uint16_t *)
     (
         packet->buffer +
         length -
         ENP_CRC_SIZE
     );
 }