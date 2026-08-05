/*
 * packets.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef PACKETS_H
 #define PACKETS_H

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 #define ENP_MAGIC              0x45534E57UL
 #define ENP_PROTOCOL_VERSION   1

 typedef enum
 {
     ENP_PACKET_SENSOR = 1,
     ENP_PACKET_ACK    = 2

 } enp_packet_type_t;

 /*------------------------------------------------------------------
  * Common Header
  *-----------------------------------------------------------------*/

 typedef struct __attribute__((packed))
 {
     uint32_t magic;

     uint8_t version;

     uint8_t type;

     uint16_t length;

     uint32_t sequence;

 } enp_header_t;

 /*------------------------------------------------------------------
  * Sensor Packet
  *-----------------------------------------------------------------*/

 typedef struct __attribute__((packed))
 {
     enp_header_t header;

     float temperature;

     float humidity;

     uint16_t crc;

 } enp_sensor_packet_t;

 /*------------------------------------------------------------------
  * ACK Packet
  *-----------------------------------------------------------------*/

 typedef struct __attribute__((packed))
 {
     enp_header_t header;

     uint32_t acknowledged_sequence;

     uint8_t status;

     uint16_t crc;

 } enp_ack_packet_t;

 /*------------------------------------------------------------------
  * Generic helpers
  *-----------------------------------------------------------------*/

 void enp_packet_finalize(void *packet, size_t packet_size);

 bool enp_packet_verify(const void *packet, size_t packet_size);

 /*------------------------------------------------------------------
  * Packet initialization
  *-----------------------------------------------------------------*/

 void enp_sensor_packet_init(enp_sensor_packet_t *packet);

 void enp_ack_packet_init(enp_ack_packet_t *packet);

 #endif