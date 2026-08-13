/*
 * enp_routing_rerr.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_routing_rerr.h"

 #include <string.h>

 static void put_u16_le(uint8_t *dst, uint16_t value)
 {
     dst[0] = (uint8_t)(value & 0xFFU);
     dst[1] = (uint8_t)((value >> 8) & 0xFFU);
 }

 static uint16_t get_u16_le(const uint8_t *src)
 {
     return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
 }

 static void put_u32_le(uint8_t *dst, uint32_t value)
 {
     dst[0] = (uint8_t)(value & 0xFFU);
     dst[1] = (uint8_t)((value >> 8) & 0xFFU);
     dst[2] = (uint8_t)((value >> 16) & 0xFFU);
     dst[3] = (uint8_t)((value >> 24) & 0xFFU);
 }

 static uint32_t get_u32_le(const uint8_t *src)
 {
     return (uint32_t)src[0] |
            ((uint32_t)src[1] << 8) |
            ((uint32_t)src[2] << 16) |
            ((uint32_t)src[3] << 24);
 }

 static bool reason_valid(uint8_t reason)
 {
     return reason >= ENP_ROUTE_ERROR_NO_ROUTE &&
            reason <= ENP_ROUTE_ERROR_TTL_EXPIRED;
 }

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL || buffer == NULL ||
         buffer_size < ENP_ROUTING_RERR_WIRE_SIZE) {
         return false;
     }

     if (message->payload_version != ENP_ROUTING_PAYLOAD_VERSION ||
         message->subtype != ENP_ROUTING_SUBTYPE_RERR ||
         message->unreachable_network_id == 0U ||
         message->unreachable_node_id == 0U ||
         !reason_valid(message->reason) ||
         message->reserved_0 != 0U ||
         message->reserved_1 != 0U) {
         return false;
     }

     buffer[0] = message->payload_version;
     buffer[1] = message->subtype;
     put_u16_le(&buffer[2], message->unreachable_network_id);
     put_u16_le(&buffer[4], message->unreachable_node_id);
     put_u32_le(&buffer[6], message->destination_sequence);
     buffer[10] = message->reason;
     buffer[11] = 0U;
     put_u32_le(&buffer[12], 0U);

     return true;
 }

 bool enp_routing_rerr_decode(
     enp_routing_rerr_t *message,
     const uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL || buffer == NULL ||
         buffer_size < ENP_ROUTING_RERR_WIRE_SIZE) {
         return false;
     }

     if (buffer[0] != ENP_ROUTING_PAYLOAD_VERSION ||
         buffer[1] != ENP_ROUTING_SUBTYPE_RERR ||
         get_u16_le(&buffer[2]) == 0U ||
         get_u16_le(&buffer[4]) == 0U ||
         !reason_valid(buffer[10]) ||
         buffer[11] != 0U ||
         get_u32_le(&buffer[12]) != 0U) {
         return false;
     }

     memset(message, 0, sizeof(*message));
     message->payload_version = buffer[0];
     message->subtype = buffer[1];
     message->unreachable_network_id = get_u16_le(&buffer[2]);
     message->unreachable_node_id = get_u16_le(&buffer[4]);
     message->destination_sequence = get_u32_le(&buffer[6]);
     message->reason = buffer[10];
     message->reserved_0 = buffer[11];
     message->reserved_1 = get_u32_le(&buffer[12]);

     return true;
 }
