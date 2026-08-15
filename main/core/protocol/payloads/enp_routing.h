/*
 * enp_routing.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 #ifndef ENP_ROUTING_H
 #define ENP_ROUTING_H

 /**
  * ENP v0.2 Routing Protocol
  *
  * Phase R1/R2:
  *   - Routing wire definitions
  *   - Explicit serialization/deserialization
  *
  * Wire sizes:
  *   RREQ = 20 bytes
  *   RREP = 20 bytes
  *   RERR = 16 bytes
  *
  * Multi-byte wire fields use little-endian byte order.
  *
  * ESP-IDF 6.02 compatible.
  */

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 /* --------------------------------------------------------------------------
  * Protocol constants
  * -------------------------------------------------------------------------- */

 #define ENP_ROUTING_PAYLOAD_VERSION       0x01U

 #define ENP_ROUTING_RREQ_WIRE_SIZE        20U
 #define ENP_ROUTING_RREP_WIRE_SIZE        20U
 #define ENP_ROUTING_RERR_WIRE_SIZE        16U

 /* --------------------------------------------------------------------------
  * Routing control subtypes
  * -------------------------------------------------------------------------- */

 typedef enum {
     ENP_ROUTING_SUBTYPE_RREQ = 1,
     ENP_ROUTING_SUBTYPE_RREP = 2,
     ENP_ROUTING_SUBTYPE_RERR = 3,
 } enp_routing_subtype_t;

 /* --------------------------------------------------------------------------
  * Routing identifiers
  *
  * These are deliberately separate from the normal ENP packet sequence
  * number used by duplicate suppression.
  * -------------------------------------------------------------------------- */

 typedef uint32_t enp_route_sequence_t;
 typedef uint32_t enp_route_request_id_t;

 /* --------------------------------------------------------------------------
  * Route error reasons
  * -------------------------------------------------------------------------- */

 typedef enum {
     ENP_ROUTE_ERROR_UNKNOWN              = 0,
     ENP_ROUTE_ERROR_NO_ROUTE             = 1,
     ENP_ROUTE_ERROR_NEXT_HOP_UNREACHABLE = 2,
     ENP_ROUTE_ERROR_ROUTE_EXPIRED        = 3,
     ENP_ROUTE_ERROR_LOCAL_REPAIR_FAILED  = 4,
     ENP_ROUTE_ERROR_TTL_EXPIRED          = 5,
 } enp_route_error_reason_t;

 /* --------------------------------------------------------------------------
  * RREQ — Route Request
  *
  * Wire layout:
  *
  *   Offset  Size  Field
  *   ------  ----  ---------------------------
  *      0     1    payload_version
  *      1     1    subtype
  *      2     2    destination_network_id
  *      4     2    destination_node_id
  *      6     4    route_request_id
  *     10     4    destination_sequence
  *     14     1    hop_count
  *     15     1    ttl
  *     16     4    route_lifetime_ms
  *
  * Total = 20 bytes
  * -------------------------------------------------------------------------- */

 typedef struct __attribute__((packed)) {
     uint8_t  payload_version;
     uint8_t  subtype;

     uint16_t destination_network_id;
     uint16_t destination_node_id;

     enp_route_request_id_t route_request_id;
     enp_route_sequence_t   destination_sequence;

     uint8_t  hop_count;
     uint8_t  ttl;

     uint32_t route_lifetime_ms;
 } enp_routing_rreq_t;

 /* --------------------------------------------------------------------------
  * RREP — Route Reply
  *
  * ENP packet header:
  *   source      = RREP generator / route destination
  *   destination = RREQ originator
  *
  * The payload destination identifies the route being established.
  * It is NOT the immediate RREP recipient.
  *
  * Wire layout:
  *
  *   Offset  Size  Field
  *   ------  ----  ---------------------------
  *      0     1    payload_version
  *      1     1    subtype
  *      2     2    destination_network_id
  *      4     2    destination_node_id
  *      6     4    destination_sequence
  *     10     1    hop_count
  *     11     1    reserved
  *     12     4    route_lifetime_ms
  *     16     4    reserved
  *
  * Total = 20 bytes
  * -------------------------------------------------------------------------- */

 typedef struct __attribute__((packed)) {
     uint8_t  payload_version;
     uint8_t  subtype;

     uint16_t destination_network_id;
     uint16_t destination_node_id;

     enp_route_sequence_t destination_sequence;

     uint8_t  hop_count;
     uint8_t  reserved_0;

     uint32_t route_lifetime_ms;

     uint32_t reserved_1;
 } enp_routing_rrep_t;

 /* --------------------------------------------------------------------------
  * RERR — Route Error
  *
  * Wire layout:
  *
  *   Offset  Size  Field
  *   ------  ----  ---------------------------
  *      0     1    payload_version
  *      1     1    subtype
  *      2     2    unreachable_network_id
  *      4     2    unreachable_node_id
  *      6     4    destination_sequence
  *     10     1    reason
  *     11     1    reserved
  *     12     4    reserved
  *
  * Total = 16 bytes
  * -------------------------------------------------------------------------- */

 typedef struct __attribute__((packed)) {
     uint8_t  payload_version;
     uint8_t  subtype;

     uint16_t unreachable_network_id;
     uint16_t unreachable_node_id;

     enp_route_sequence_t destination_sequence;

     uint8_t  reason;
     uint8_t  reserved_0;

     uint32_t reserved_1;
 } enp_routing_rerr_t;

 /* --------------------------------------------------------------------------
  * Compile-time wire-size validation
  * -------------------------------------------------------------------------- */

 _Static_assert(
     sizeof(enp_routing_rreq_t) == ENP_ROUTING_RREQ_WIRE_SIZE,
     "ENP RREQ wire size must be exactly 20 bytes");

 _Static_assert(
     sizeof(enp_routing_rrep_t) == ENP_ROUTING_RREP_WIRE_SIZE,
     "ENP RREP wire size must be exactly 20 bytes");

 _Static_assert(
     sizeof(enp_routing_rerr_t) == ENP_ROUTING_RERR_WIRE_SIZE,
     "ENP RERR wire size must be exactly 16 bytes");

 /* --------------------------------------------------------------------------
  * RREQ serialization
  * -------------------------------------------------------------------------- */

 bool enp_routing_rreq_encode(
     const enp_routing_rreq_t *message,
     uint8_t *buffer,
     size_t buffer_size);

 bool enp_routing_rreq_decode(
     enp_routing_rreq_t *message,
     const uint8_t *buffer,
     size_t buffer_size);

 /* --------------------------------------------------------------------------
  * RREP serialization
  * -------------------------------------------------------------------------- */

 bool enp_routing_rrep_encode(
     const enp_routing_rrep_t *message,
     uint8_t *buffer,
     size_t buffer_size);

 bool enp_routing_rrep_decode(
     enp_routing_rrep_t *message,
     const uint8_t *buffer,
     size_t buffer_size);

 /* --------------------------------------------------------------------------
  * RERR serialization
  * -------------------------------------------------------------------------- */

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size);

 bool enp_routing_rerr_decode(
     enp_routing_rerr_t *message,
     const uint8_t *buffer,
     size_t buffer_size);

 #endif /* ENP_ROUTING_H */