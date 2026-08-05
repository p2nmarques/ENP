/*
 * packets.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */


 #include "packets.h"

 #include <stddef.h>
 #include <stdint.h>
 #include <stdlib.h>
 #include <string.h>

 #include "crc16.h"

 /*------------------------------------------------------------------
  * Internal CRC helper
  *-----------------------------------------------------------------*/

 static uint16_t enp_packet_crc(const void *data, size_t length)
 {
     return enp_crc16_ccitt(data, length);
 }

 /*------------------------------------------------------------------
  * Generic helpers
  *-----------------------------------------------------------------*/

 void enp_packet_finalize(void *packet, size_t packet_size)
 {
     uint8_t *bytes = (uint8_t *)packet;

     /* CRC is always the last field */
     uint16_t *crc =
         (uint16_t *)(bytes + packet_size - sizeof(uint16_t));

     *crc = 0;

     *crc = enp_packet_crc(packet, packet_size);
 }

 bool enp_packet_verify(const void *packet, size_t packet_size)
 {
     uint8_t copy[64];

     if (packet_size > sizeof(copy))
     {
         return false;
     }

     memcpy(copy, packet, packet_size);

     uint16_t *crc =
         (uint16_t *)(copy + packet_size - sizeof(uint16_t));

     uint16_t received = *crc;

     *crc = 0;

     uint16_t calculated =
         enp_packet_crc(copy, packet_size);

     return received == calculated;
 }

 /*------------------------------------------------------------------
  * Sensor
  *-----------------------------------------------------------------*/

 void enp_sensor_packet_init(enp_sensor_packet_t *packet)
 {
     memset(packet, 0, sizeof(*packet));

     packet->header.magic = ENP_MAGIC;
     packet->header.version = ENP_PROTOCOL_VERSION;
     packet->header.type = ENP_PACKET_SENSOR;
     packet->header.length = sizeof(enp_sensor_packet_t);
 }

 /*------------------------------------------------------------------
  * ACK
  *-----------------------------------------------------------------*/

 void enp_ack_packet_init(enp_ack_packet_t *packet)
 {
     memset(packet, 0, sizeof(*packet));

     packet->header.magic = ENP_MAGIC;
     packet->header.version = ENP_PROTOCOL_VERSION;
     packet->header.type = ENP_PACKET_ACK;
     packet->header.length = sizeof(enp_ack_packet_t);
 }