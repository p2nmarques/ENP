/*
 * enp_routing_rerr.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #ifndef ENP_ROUTING_RERR_H
 #define ENP_ROUTING_RERR_H

 #include <stdbool.h>
 #include <stddef.h>
 #include <stdint.h>

 #define ENP_ROUTING_PAYLOAD_VERSION 1U
 #define ENP_ROUTING_SUBTYPE_RERR 3U
 #define ENP_ROUTING_RERR_WIRE_SIZE 16U

 typedef struct __attribute__((packed)) {
     uint8_t subtype;
     uint8_t flags;
     uint8_t version;
     uint8_t reserved;

     uint16_t unreachable_network_id;
     uint16_t unreachable_node_id;

     uint32_t destination_sequence;

     uint8_t reason;
     uint8_t reason_reserved;
 } enp_routing_rerr_t;

 typedef enum {
     ENP_RERR_REASON_NEXT_HOP_UNAVAILABLE = 1,
     ENP_RERR_REASON_TRANSPORT_FAILURE = 2,
     ENP_RERR_REASON_ROUTE_EXPIRED = 3,
     ENP_RERR_REASON_LOCAL_REPAIR_FAILED = 4,
     ENP_RERR_REASON_POLICY_INVALIDATED = 5,
 } enp_rerr_reason_t;

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size);

 bool enp_routing_rerr_decode(
     enp_routing_rerr_t *message,
     const uint8_t *buffer,
     size_t buffer_size);

 #endif
