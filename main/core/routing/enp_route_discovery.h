/*
 * enp_route_discovery.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #ifndef ENP_ROUTE_DISCOVERY_H
 #define ENP_ROUTE_DISCOVERY_H

 /**
  * ENP v0.2 Route Discovery State Machine — R4-A.1
  *
  * R4-A manages the lifecycle of one route-discovery transaction.
  *
  * R4-A deliberately does NOT:
  *   - send or receive packets;
  *   - access the transport;
  *   - modify the route table;
  *   - implement RREQ/RREP processing;
  *   - allocate dynamic memory;
  *   - depend on FreeRTOS.
  *
  * RREP correlation:
  *   The ENP v0.2 RREP wire payload does not carry route_request_id.
  *   RREP completion is therefore correlated using the requested
  *   destination and destination sequence number. A received RREP must
  *   carry a sequence equal to or newer than the sequence requested.
  */

 #include <stdbool.h>
 #include <stdint.h>

 #ifndef ENP_MAX_RETRIES
 #define ENP_MAX_RETRIES 3U
 #endif

 #ifndef ENP_DISCOVERY_TIMEOUT_MS
 #define ENP_DISCOVERY_TIMEOUT_MS 2000U
 #endif

 #ifndef ENP_DISCOVERY_DEFAULT_TTL
 #define ENP_DISCOVERY_DEFAULT_TTL 8U
 #endif

 typedef enum {
     ENP_DISCOVERY_STATE_IDLE = 0,
     ENP_DISCOVERY_STATE_REQUESTING,
     ENP_DISCOVERY_STATE_COMPLETE,
     ENP_DISCOVERY_STATE_FAILED,
 } enp_discovery_state_t;

 typedef struct {
     uint16_t network_id;
     uint16_t node_id;
 } enp_discovery_destination_t;

 typedef struct {
     enp_discovery_state_t state;

     enp_discovery_destination_t destination;

     /* Used to identify the originating RREQ transaction locally. */
     uint32_t route_request_id;
     uint32_t destination_sequence;

     uint8_t ttl;
     uint8_t retry_count;

     uint32_t started_at_ms;
     uint32_t deadline_ms;
 } enp_route_discovery_t;

 bool enp_route_discovery_init(
     enp_route_discovery_t *discovery);

 bool enp_route_discovery_start(
     enp_route_discovery_t *discovery,
     enp_discovery_destination_t destination,
     uint32_t route_request_id,
     uint32_t destination_sequence,
     uint8_t ttl,
     uint32_t now_ms);

 /**
  * Handle an RREP result for the active transaction.
  *
  * RREP wire format has no route_request_id. The RREP is accepted when:
  *   1. its route destination matches the active discovery destination; and
  *   2. its destination sequence is equal to or newer than the requested
  *      destination sequence.
  *
  * Sequence comparison uses RFC-style serial-number arithmetic and is
  * therefore safe across uint32_t wrap-around for differences < 2^31.
  */
 bool enp_route_discovery_on_rrep(
     enp_route_discovery_t *discovery,
     enp_discovery_destination_t destination,
     uint32_t destination_sequence);

 bool enp_route_discovery_on_timeout(
     enp_route_discovery_t *discovery,
     uint32_t now_ms);

 bool enp_route_discovery_is_active(
     const enp_route_discovery_t *discovery);

 enp_discovery_state_t enp_route_discovery_state(
     const enp_route_discovery_t *discovery);

 #endif /* ENP_ROUTE_DISCOVERY_H */
