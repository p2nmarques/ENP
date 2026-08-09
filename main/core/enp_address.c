/*
 * enp_address.c
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_packet.c
  *
  * @brief ENP generic packet implementation.
  */

 #include "enp_packet.h"

 #include <string.h>

 /*----------------------------------------------------------
  * Private Constants
  *---------------------------------------------------------*/

 /**
  * @brief Serialized ENP header size.
  */
 #define ENP_HEADER_SIZE \
     ((size_t)sizeof(enp_header_t))

 /**
  * @brief Serialized CRC16 size.
  */
 #define ENP_CRC_SIZE \
     ((size_t)sizeof(uint16_t))

 /**
  * @brief Number of non-payload bytes in an ENP frame.
  */
 #define ENP_PACKET_OVERHEAD \
     (ENP_HEADER_SIZE + ENP_CRC_SIZE)

 /**
  * @brief Minimum valid ENP frame size.
  */
 #define ENP_PACKET_MIN_SIZE \
     ENP_PACKET_OVERHEAD

 /**
  * @brief CRC-16/CCITT-FALSE polynomial.
  */
 #define ENP_CRC16_POLYNOMIAL   0x1021U

 /**
  * @brief CRC-16/CCITT-FALSE initial value.
  */
 #define ENP_CRC16_INITIAL      0xFFFFU

 /*----------------------------------------------------------
  * Compile-Time Validation
  *---------------------------------------------------------*/

 _Static_assert(
         sizeof(enp_address_t) == 6U,
         "Unexpected ENP address size");

 _Static_assert(
         sizeof(enp_header_t) == 26U,
         "Unexpected ENP header size");

 _Static_assert(
         sizeof(enp_sequence_t) == sizeof(uint32_t),
         "Unexpected ENP sequence size");

 _Static_assert(
         sizeof(enp_node_id_t) == sizeof(uint32_t),
         "Unexpected ENP node ID size");

 _Static_assert(
         sizeof(enp_network_id_t) == sizeof(uint16_t),
         "Unexpected ENP network ID size");

 _Static_assert(
         ENP_PACKET_OVERHEAD < ENP_MAX_PACKET_SIZE,
         "ENP packet overhead exceeds maximum packet size");

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static uint16_t enp_crc16(
         const uint8_t *data,
         size_t length);

 static void enp_write_u16_le(
         uint8_t *destination,
         uint16_t value);

 static uint16_t enp_read_u16_le(
         const uint8_t *source);

 static uint16_t *enp_packet_crc(
         enp_packet_t *packet);

 static const uint8_t *enp_packet_crc_const(
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

     const size_t length =
             ENP_PACKET_OVERHEAD +
             (size_t)header->payload_length;

     if (length > ENP_MAX_PACKET_SIZE)
     {
         return 0U;
     }

     return length;
 }

 esp_err_t enp_packet_seal(
         enp_packet_t *packet,
         uint16_t payload_length)
 {
     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     const size_t frame_length =
             ENP_PACKET_OVERHEAD +
             (size_t)payload_length;

     if (frame_length > ENP_MAX_PACKET_SIZE)
     {
         return ESP_ERR_INVALID_SIZE;
     }

     enp_header_t *header =
             enp_packet_header(packet);

     if (header == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     /*
      * The packet must have been initialized through
      * enp_packet_init().
      */
     if (header->magic != ENP_PROTOCOL_MAGIC)
     {
         return ESP_ERR_INVALID_STATE;
     }

     if (header->version != ENP_PROTOCOL_VERSION)
     {
         return ESP_ERR_INVALID_STATE;
     }

     if (header->type == ENP_PACKET_INVALID)
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (header->ttl > ENP_MAX_TTL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     header->payload_length = payload_length;

     uint8_t *crc_location =
             packet->buffer +
             frame_length -
             ENP_CRC_SIZE;

     const uint16_t crc =
             enp_crc16(
                     packet->buffer,
                     frame_length - ENP_CRC_SIZE);

     enp_write_u16_le(
             crc_location,
             crc);

     return ESP_OK;
 }

 bool enp_packet_verify(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
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

     if (header->type == ENP_PACKET_INVALID)
     {
         return false;
     }

     if (header->ttl > ENP_MAX_TTL)
     {
         return false;
     }

     const size_t frame_length =
             ENP_PACKET_OVERHEAD +
             (size_t)header->payload_length;

     if ((frame_length < ENP_PACKET_MIN_SIZE) ||
         (frame_length > ENP_MAX_PACKET_SIZE))
     {
         return false;
     }

     const uint8_t *crc_location =
             packet->buffer +
             frame_length -
             ENP_CRC_SIZE;

     const uint16_t received_crc =
             enp_read_u16_le(crc_location);

     const uint16_t calculated_crc =
             enp_crc16(
                     packet->buffer,
                     frame_length - ENP_CRC_SIZE);

     return received_crc == calculated_crc;
 }

 /*----------------------------------------------------------
  * Private CRC Implementation
  *---------------------------------------------------------*/

 /**
  * @brief Calculate CRC-16/CCITT-FALSE.
  *
  * Parameters:
  *
  * Polynomial : 0x1021
  * Initial     : 0xFFFF
  * RefIn       : false
  * RefOut      : false
  * XorOut     : 0x0000
  */
 static uint16_t enp_crc16(
         const uint8_t *data,
         size_t length)
 {
     uint16_t crc = ENP_CRC16_INITIAL;

     if ((data == NULL) && (length != 0U))
     {
         return 0U;
     }

     for (size_t index = 0U;
          index < length;
          ++index)
     {
         crc ^= (uint16_t)data[index] << 8U;

         for (uint8_t bit = 0U;
              bit < 8U;
              ++bit)
         {
             if ((crc & 0x8000U) != 0U)
             {
                 crc =
                         (uint16_t)
                         ((crc << 1U) ^
                          ENP_CRC16_POLYNOMIAL);
             }
             else
             {
                 crc =
                         (uint16_t)
                         (crc << 1U);
             }
         }
     }

     return crc;
 }

 /*----------------------------------------------------------
  * Private Serialization Helpers
  *---------------------------------------------------------*/

 static void enp_write_u16_le(
         uint8_t *destination,
         uint16_t value)
 {
     destination[0] =
             (uint8_t)(value & 0xFFU);

     destination[1] =
             (uint8_t)((value >> 8U) & 0xFFU);
 }

 static uint16_t enp_read_u16_le(
         const uint8_t *source)
 {
     return (uint16_t)
            ((uint16_t)source[0] |
             ((uint16_t)source[1] << 8U));
 }

 /*----------------------------------------------------------
  * Private CRC Access
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
             (packet->buffer +
              length -
              ENP_CRC_SIZE);
 }

 static const uint8_t *enp_packet_crc_const(
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

     return packet->buffer +
            length -
            ENP_CRC_SIZE;
 }