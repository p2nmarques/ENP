/*
 * en_dispatcher.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

#ifndef EN_DISPATCHER_H
#define EN_DISPATCHER_H

#include "esp_err.h"

#include "/core/enp_context.h"
#include "core/service/enp_service.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t enp_dispatcher_init(
        enp_context_t *context);

esp_err_t enp_dispatcher_register(
        const enp_service_t *service);

esp_err_t enp_dispatcher_dispatch(
        const enp_packet_t *packet);

#ifdef __cplusplus
}
#endif

#endif