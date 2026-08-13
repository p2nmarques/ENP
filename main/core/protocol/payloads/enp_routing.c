/*
 * enp_routing.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_routing.h"

 #include <string.h>

 /*
  * ENP v0.2 Routing Protocol — R2
  *
  * Explicit serialization/deserialization.
  *
  * Wire byte order:
  *     little-endian
  *
  * No direct structure-to-wire memcpy is used for multi-byte fields.
  * This keeps the protocol independent of compiler ABI and alignment.
  */

 /* --------------------------------------------------------------------------
  * Local helpers
  * -------------------------------------------------------------------------- */

 static void put_u16_le(uint8_t *dst, uint16_t value)
 {
     dst[0] = (uint8_t)(value & 0xFFU);
     dst[1] = (uint8_t)((value >> 8) & 0xFFU);
 }

 static uint16_t get_u16_le(const uint8_t *src)
 {
     return (uint16_t)src[0] |
            ((uint16_t)src[1] << 8);
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

 static bool valid_common(
     const uint8_t *buffer,
     size_t buffer_size,
     size_t required_size,
     uint8_t expected_subtype)
 {
     if (buffer == NULL || buffer_size < required_size) {
         return false;
     }

     if (buffer[0] != ENP_ROUTING_PAYLOAD_VERSION) {
         return false;
     }

     if (buffer[1] != expected_subtype) {
         return false;
     }

     return true;
 }

 /* --------------------------------------------------------------------------
  * RREQ
  * -------------------------------------------------------------------------- */

 bool enp_routing_rreq_encode(
     const enp_routing_rreq_t *message,
     uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL ||
         buffer == NULL ||
         buffer_size < ENP_ROUTING_RREQ_WIRE_SIZE) {
         return false;
     }

     buffer[0] = ENP_ROUTING_PAYLOAD_VERSION;
     buffer[1] = (uint8_t)ENP_ROUTING_SUBTYPE_RREQ;

     put_u16_le(&buffer[2], message->destination_network_id);
     put_u16_le(&buffer[4], message->destination_node_id);

     put_u32_le(&buffer[6], message->route_request_id);
     put_u32_le(&buffer[10], message->destination_sequence);

     buffer[14] = message->hop_count;
     buffer[15] = message->ttl;

     put_u32_le(&buffer[16], message->route_lifetime_ms);

     return true;
 }

 bool enp_routing_rreq_decode(
     enp_routing_rreq_t *message,
     const uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL ||
         !valid_common(
             buffer,
             buffer_size,
             ENP_ROUTING_RREQ_WIRE_SIZE,
             (uint8_t)ENP_ROUTING_SUBTYPE_RREQ)) {
         return false;
     }

     memset(message, 0, sizeof(*message));

     message->payload_version = buffer[0];
     message->subtype = buffer[1];

     message->destination_network_id = get_u16_le(&buffer[2]);
     message->destination_node_id = get_u16_le(&buffer[4]);

     message->route_request_id = get_u32_le(&buffer[6]);
     message->destination_sequence = get_u32_le(&buffer[10]);

     message->hop_count = buffer[14];
     message->ttl = buffer[15];

     message->route_lifetime_ms = get_u32_le(&buffer[16]);

     return true;
 }

 /* --------------------------------------------------------------------------
  * RREP
  * -------------------------------------------------------------------------- */

 bool enp_routing_rrep_encode(
     const enp_routing_rrep_t *message,
     uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL ||
         buffer == NULL ||
         buffer_size < ENP_ROUTING_RREP_WIRE_SIZE) {
         return false;
     }

     buffer[0] = ENP_ROUTING_PAYLOAD_VERSION;
     buffer[1] = (uint8_t)ENP_ROUTING_SUBTYPE_RREP;

     put_u16_le(&buffer[2], message->destination_network_id);
     put_u16_le(&buffer[4], message->destination_node_id);

     put_u32_le(&buffer[6], message->destination_sequence);

     buffer[10] = message->hop_count;
     buffer[11] = 0U;

     put_u32_le(&buffer[12], message->route_lifetime_ms);

     put_u32_le(&buffer[16], 0U);

     return true;
 }

 bool enp_routing_rrep_decode(
     enp_routing_rrep_t *message,
     const uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL ||
         !valid_common(
             buffer,
             buffer_size,
             ENP_ROUTING_RREP_WIRE_SIZE,
             (uint8_t)ENP_ROUTING_SUBTYPE_RREP)) {
         return false;
     }

     memset(message, 0, sizeof(*message));

     message->payload_version = buffer[0];
     message->subtype = buffer[1];

     message->destination_network_id = get_u16_le(&buffer[2]);
     message->destination_node_id = get_u16_le(&buffer[4]);

     message->destination_sequence = get_u32_le(&buffer[6]);

     message->hop_count = buffer[10];
     message->reserved_0 = buffer[11];

     message->route_lifetime_ms = get_u32_le(&buffer[12]);

     message->reserved_1 = get_u32_le(&buffer[16]);

     return true;
 }

 /* --------------------------------------------------------------------------
  * RERR
  * -------------------------------------------------------------------------- */

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size)
 {
     if (message == NULL ||
         buffer == NULL ||
         buffer_size < ENP_ROUTING_RERR_WIRE_SIZE) {
         return false;
     }

     buffer[0] = ENP_ROUTING_PAYLOAD_VERSION;
     buffer[1] = (uint8_t)ENP_ROUTING_SUBTYPE_RERR;

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
     if (message == NULL ||
         !valid_common(
             buffer,
             buffer_size,
             ENP_ROUTING_RERR_WIRE_SIZE,
             (uint8_t)ENP_ROUTING_SUBTYPE_RERR)) {
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


