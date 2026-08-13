/*
 * enp_route_discovery.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_route_discovery.h"

 #include <string.h>

 static bool destination_equal(
     enp_discovery_destination_t lhs,
     enp_discovery_destination_t rhs)
 {
     return lhs.network_id == rhs.network_id &&
            lhs.node_id == rhs.node_id;
 }

 static bool destination_valid(
     enp_discovery_destination_t destination)
 {
     return !(destination.network_id == 0U &&
              destination.node_id == 0U);
 }

 static uint32_t deadline_from_now(uint32_t now_ms)
 {
     return now_ms + ENP_DISCOVERY_TIMEOUT_MS;
 }

 /*
  * Return true when candidate is equal to or newer than reference.
  *
  * This is serial-number arithmetic over uint32_t. It is valid as long as
  * the sequence-number distance being compared is less than 2^31.
  */
 static bool sequence_at_least(
     uint32_t candidate,
     uint32_t reference)
 {
     return candidate == reference ||
            (int32_t)(candidate - reference) > 0;
 }

 bool enp_route_discovery_init(
     enp_route_discovery_t *discovery)
 {
     if (discovery == NULL) {
         return false;
     }

     memset(discovery, 0, sizeof(*discovery));
     discovery->state = ENP_DISCOVERY_STATE_IDLE;

     return true;
 }

 bool enp_route_discovery_start(
     enp_route_discovery_t *discovery,
     enp_discovery_destination_t destination,
     uint32_t route_request_id,
     uint32_t destination_sequence,
     uint8_t ttl,
     uint32_t now_ms)
 {
     if (discovery == NULL ||
         !destination_valid(destination) ||
         route_request_id == 0U ||
         ttl == 0U) {
         return false;
     }

     if (discovery->state == ENP_DISCOVERY_STATE_REQUESTING) {
         return false;
     }

     discovery->state = ENP_DISCOVERY_STATE_REQUESTING;
     discovery->destination = destination;
     discovery->route_request_id = route_request_id;
     discovery->destination_sequence = destination_sequence;
     discovery->ttl = ttl;
     discovery->retry_count = 0U;
     discovery->started_at_ms = now_ms;
     discovery->deadline_ms = deadline_from_now(now_ms);

     return true;
 }

 bool enp_route_discovery_on_rrep(
     enp_route_discovery_t *discovery,
     enp_discovery_destination_t destination,
     uint32_t destination_sequence)
 {
     if (discovery == NULL ||
         discovery->state != ENP_DISCOVERY_STATE_REQUESTING) {
         return false;
     }

     if (!destination_equal(discovery->destination, destination)) {
         return false;
     }

     if (!sequence_at_least(
             destination_sequence,
             discovery->destination_sequence)) {
         return false;
     }

     discovery->state = ENP_DISCOVERY_STATE_COMPLETE;

     return true;
 }

 bool enp_route_discovery_on_timeout(
     enp_route_discovery_t *discovery,
     uint32_t now_ms)
 {
     if (discovery == NULL ||
         discovery->state != ENP_DISCOVERY_STATE_REQUESTING) {
         return false;
     }

     if ((int32_t)(now_ms - discovery->deadline_ms) < 0) {
         return false;
     }

     if (discovery->retry_count < ENP_MAX_RETRIES) {
         ++discovery->retry_count;
         discovery->deadline_ms = deadline_from_now(now_ms);
         return true;
     }

     discovery->state = ENP_DISCOVERY_STATE_FAILED;

     return false;
 }

 bool enp_route_discovery_is_active(
     const enp_route_discovery_t *discovery)
 {
     return discovery != NULL &&
            discovery->state == ENP_DISCOVERY_STATE_REQUESTING;
 }

 enp_discovery_state_t enp_route_discovery_state(
     const enp_route_discovery_t *discovery)
 {
     if (discovery == NULL) {
         return ENP_DISCOVERY_STATE_FAILED;
     }

     return discovery->state;
 }
