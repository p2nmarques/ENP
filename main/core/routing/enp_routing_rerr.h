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

 #include "core/protocol/payloads/enp_routing.h"

 /* R4-D uses the canonical R2 RERR wire definition. */

 bool enp_routing_rerr_encode(
     const enp_routing_rerr_t *message,
     uint8_t *buffer,
     size_t buffer_size);

 bool enp_routing_rerr_decode(
     enp_routing_rerr_t *message,
     const uint8_t *buffer,
     size_t buffer_size);

 #endif