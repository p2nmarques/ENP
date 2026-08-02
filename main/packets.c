/*
 * packets.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */


 #include <string.h>

 #include "packets.h"
 #include "crc16.h"

 void espnow_packet_init(
         espnow_packet_t *packet,
         espnow_packet_type_t type,
         uint32_t sequence)
 {
     memset(packet, 0, sizeof(*packet));

     packet->header.magic = ESPNOW_MAGIC;

     packet->header.version = ESPNOW_PROTOCOL_VERSION;

     packet->header.type = type;

     packet->header.length = sizeof(*packet);

     packet->header.sequence = sequence;
 }

 void espnow_packet_finalize(
         espnow_packet_t *packet)
 {
     packet->crc = 0;

     packet->crc = crc16_ccitt(
             packet,
             sizeof(*packet) - sizeof(packet->crc));
 }

 bool espnow_packet_verify(
         const espnow_packet_t *packet)
 {
     espnow_packet_t tmp = *packet;

     uint16_t received = tmp.crc;

     tmp.crc = 0;

     uint16_t calculated =
         crc16_ccitt(
             &tmp,
             sizeof(tmp) - sizeof(tmp.crc));

     return received == calculated;
 }

