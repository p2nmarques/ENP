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

 #define ENP_HEADER_SIZE        ((size_t)sizeof(enp_header_t))
 #define ENP_CRC_SIZE           ((size_t)sizeof(uint16_t))

 #define ENP_PACKET_OVERHEAD    (ENP_HEADER_SIZE + ENP_CRC_SIZE)

 #define ENP_PACKET_MIN_SIZE    ENP_PACKET_OVERHEAD

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static uint16_t *packet_crc(
         enp_packet_t *packet);

 static const uint16_t *packet_crc_const(
         const enp_packet_t *packet);

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 void enp_packet_init(
         enp_packet_t *packet,
         enp_packet_type_t type,
         enp_node_id_t source)
 {
     if (packet == NULL)
     {
         return;
     }

     memset(packet, 0, sizeof(*packet));

     enp_header_t *header =
             enp_packet_header(packet);

     header->magic = ENP_PACKET_MAGIC;

     header->version = ENP_PROTOCOL_VERSION_MINOR;

     header->type = (uint8_t)type;

     header->flags = ENP_FLAG_NONE;

     header->ttl = ENP_DEFAULT_TTL;

     header->source = source;
 }

 void *enp_packet_data(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->data;
 }

 const void *enp_packet_data_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return packet->data;
 }

 enp_header_t *enp_packet_header(
         enp_packet_t *packet)
 {
     return (enp_header_t *)enp_packet_data(packet);
 }

 const enp_header_t *enp_packet_header_const(
         const enp_packet_t *packet)
 {
     return (const enp_header_t *)enp_packet_data_const(packet);
 }

 void *enp_packet_payload(
         enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return ((uint8_t *)enp_packet_data(packet)) +
            ENP_HEADER_SIZE;
 }

 const void *enp_packet_payload_const(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return NULL;
     }

     return ((const uint8_t *)enp_packet_data_const(packet)) +
            ENP_HEADER_SIZE;
 }

 size_t enp_packet_length(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return 0;
     }

     const enp_header_t *header =
             enp_packet_header_const(packet);

     return ENP_PACKET_OVERHEAD +
            header->payload_length;
 }

 esp_err_t enp_packet_seal(
         enp_packet_t *packet,
         uint16_t payload_length)
 {
     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     enp_header_t *header =
             enp_packet_header(packet);

     header->payload_length = payload_length;

     const size_t length =
             enp_packet_length(packet);

     if (length > ENP_MAX_PACKET_SIZE)
     {
         return ESP_ERR_INVALID_SIZE;
     }

     *packet_crc(packet) =
             enp_crc16(
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

     if ((header->magic != ENP_PACKET_MAGIC) ||
         (header->version != ENP_PROTOCOL_VERSION_MINOR))
     {
         return false;
     }

     const uint16_t calculated =
             enp_crc16(
                     enp_packet_data_const(packet),
                     length - ENP_CRC_SIZE);

     return (calculated ==
             *packet_crc_const(packet));
 }

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static uint16_t *packet_crc(
         enp_packet_t *packet)
 {
     return (uint16_t *)
     (
         ((uint8_t *)enp_packet_data(packet)) +
         enp_packet_length(packet) -
         ENP_CRC_SIZE
     );
 }

 static const uint16_t *packet_crc_const(
         const enp_packet_t *packet)
 {
     return (const uint16_t *)
     (
         ((const uint8_t *)enp_packet_data_const(packet)) +
         enp_packet_length(packet) -
         ENP_CRC_SIZE
     );
 }