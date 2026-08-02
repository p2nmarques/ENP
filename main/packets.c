/*
 * packets.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */


 #include <string.h>

 #include "packets.h"
 #include "crc16.h"

 static uint16_t packet_crc(const void *packet, size_t len)
 {
     return crc16_ccitt(packet, len);
 }

 void sensor_packet_init(sensor_packet_t *pkt)
 {
     memset(pkt, 0, sizeof(*pkt));

     pkt->header.magic = ESPNOW_MAGIC;
     pkt->header.version = ESPNOW_PROTOCOL_VERSION;
     pkt->header.type = ESPNOW_PACKET_SENSOR;
     pkt->header.length = sizeof(sensor_packet_t);
 }

 void ack_packet_init(ack_packet_t *pkt)
 {
     memset(pkt, 0, sizeof(*pkt));

     pkt->header.magic = ESPNOW_MAGIC;
     pkt->header.version = ESPNOW_PROTOCOL_VERSION;
     pkt->header.type = ESPNOW_PACKET_ACK;
     pkt->header.length = sizeof(ack_packet_t);
 }

 bool sensor_packet_verify(const sensor_packet_t *pkt)
 {
     sensor_packet_t copy = *pkt;

     uint16_t crc = copy.crc;

     copy.crc = 0;

     return crc ==
            packet_crc(&copy,
                       sizeof(copy));
 }

 bool ack_packet_verify(const ack_packet_t *pkt)
 {
     ack_packet_t copy = *pkt;

     uint16_t crc = copy.crc;

     copy.crc = 0;

     return crc ==
            packet_crc(&copy,
                       sizeof(copy));
 }


 void sensor_packet_finalize(sensor_packet_t *pkt)
 {
     pkt->crc = 0;

     pkt->crc = crc16_ccitt(pkt, sizeof(*pkt));
 }

 
 void ack_packet_finalize(ack_packet_t *pkt)
 {
     pkt->crc = 0;

     pkt->crc = crc16_ccitt(pkt, sizeof(*pkt));
 }