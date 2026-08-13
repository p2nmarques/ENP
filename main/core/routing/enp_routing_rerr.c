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

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL || buffer == NULL ||
         buffer_size < ENP_ROUTING_RERR_WIRE_SIZE) {
         return false;
     }

     buffer[0] = ENP_ROUTING_SUBTYPE_RERR;
     buffer[1] = message->flags;
     buffer[2] = ENP_ROUTING_PAYLOAD_VERSION;
     buffer[3] = 0U;

     put_u16_le(&buffer[4], message->unreachable_network_id);
     put_u16_le(&buffer[6], message->unreachable_node_id);
     put_u32_le(&buffer[8], message->destination_sequence);

     buffer[12] = message->reason;
     buffer[13] = 0U;
     buffer[14] = 0U;
     buffer[15] = 0U;

     return true;
 }

 bool enp_routing_rerr_decode(
     enp_routing_rerr_t *message,
     const uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL || buffer == NULL ||
         buffer_size < ENP_ROUTING_RERR_WIRE_SIZE ||
         buffer[0] != ENP_ROUTING_SUBTYPE_RERR ||
         buffer[2] != ENP_ROUTING_PAYLOAD_VERSION ||
         buffer[3] != 0U ||
         buffer[13] != 0U ||
         buffer[14] != 0U ||
         buffer[15] != 0U) {
         return false;
     }

     if (buffer[12] < ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE ||
         buffer[12] > ENP_RERR_REASON_POLICY_INVALIDATED) {
         return false;
     }

     memset(message, 0, sizeof(*message));

     message->subtype = buffer[0];
     message->flags = buffer[1];
     message->version = buffer[2];
     message->reserved = buffer[3];

     message->unreachable_network_id = get_u16_le(&buffer[4]);
     message->unreachable_node_id = get_u16_le(&buffer[6]);
     message->destination_sequence = get_u32_le(&buffer[8]);

     message->reason = buffer[12];

     return true;
 }



