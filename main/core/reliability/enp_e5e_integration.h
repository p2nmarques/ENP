/*
 * enp_e5e_integration.h
 *
 *  Created on: Aug 24, 2026
 *      Author: Pedro Marques
 */

#ifndef ENP_E5E_INTEGRATION_H
#define ENP_E5E_INTEGRATION_H

#include <stdbool.h>

#include "core/reliability/enp_e5e.h"
#include "core/routing/enp_routing_data_path.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Wire the frozen Reliability, routing, transport and E5E boundaries.
 * E5D is represented only by the generic repair-request callback supplied
 * by the integration owner.
 */
bool enp_e5e_integration_init(
    enp_routing_data_path_t *routing_path,
    enp_e5e_repair_request_fn repair_request,
    void *repair_request_context);

void enp_e5e_integration_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
