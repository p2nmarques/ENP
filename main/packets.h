/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef PACKETS_H
 #define PACKETS_H

 #include <stdint.h>
 #include <stdbool.h>

 #define ESPNOW_MAGIC              0x45534E57UL
 #define ESPNOW_PROTOCOL_VERSION   1

 typedef enum
 {
     ESPNOW_PACKET_SENSOR = 1,
     ESPNOW_PACKET_ACK    = 2

 } espnow_packet_type_t;

 typedef struct __attribute__((packed))
 {
     uint32_t magic;
     uint8_t  version;
     uint8_t  type;
     uint16_t length;
     uint32_t sequence;
 } espnow_header_t;

 typedef struct __attribute__((packed))
 {
     espnow_header_t header;

     float temperature;
     float humidity;

     uint16_t crc;

 } sensor_packet_t;

 typedef struct __attribute__((packed))
 {
     espnow_header_t header;

     uint32_t acknowledged_sequence;
     uint8_t status;

     uint16_t crc;

 } ack_packet_t;

 void sensor_packet_init(sensor_packet_t *pkt);

 void ack_packet_init(ack_packet_t *pkt);

 bool sensor_packet_verify(const sensor_packet_t *pkt);

 bool ack_packet_verify(const ack_packet_t *pkt);

 #endif