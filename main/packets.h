/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef ESPNOW_PACKETS_H
 #define ESPNOW_PACKETS_H

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include <stdint.h>
 #include <stdbool.h>

 #define ESPNOW_MAGIC              0x45534E57UL
 #define ESPNOW_PROTOCOL_VERSION   1

 typedef enum
 {
     ESPNOW_PACKET_INVALID = 0,
     ESPNOW_PACKET_SENSOR  = 1,
     ESPNOW_PACKET_ACK     = 2,

 } espnow_packet_type_t;

 typedef struct __attribute__((packed))
 {
     uint32_t magic;

     uint8_t version;

     uint8_t type;

     uint16_t length;

     uint32_t sequence;

 } espnow_header_t;

 typedef struct __attribute__((packed))
 {
     float temperature;

     float humidity;

 } sensor_payload_t;

 typedef struct __attribute__((packed))
 {
     uint32_t acknowledged_sequence;

     uint8_t status;

 } ack_payload_t;

 typedef struct __attribute__((packed))
 {
     espnow_header_t header;

     union
     {
         sensor_payload_t sensor;

         ack_payload_t ack;

     } payload;

     uint16_t crc;

 } espnow_packet_t;

 /* API */

 void espnow_packet_init(
         espnow_packet_t *packet,
         espnow_packet_type_t type,
         uint32_t sequence);

 void espnow_packet_finalize(
         espnow_packet_t *packet);

 bool espnow_packet_verify(
         const espnow_packet_t *packet);

 #ifdef __cplusplus
 }
 #endif

 #endif
