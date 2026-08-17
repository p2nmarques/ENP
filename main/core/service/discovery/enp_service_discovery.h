/*
 * enp_service_discovery.h
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_service_discovery.h
 *
 * @brief ENP discovery service.
 */

#ifndef ENP_SERVICE_DISCOVERY_H
#define ENP_SERVICE_DISCOVERY_H

#include "core/service/enp_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the ENP discovery service descriptor.
 *
 * @return Pointer to the statically allocated service.
 */
const enp_service_t *enp_service_discovery_get(void);

/**
 * @brief Send a discovery announcement.
 *
 * The announcement is sent using the transport broadcast
 * address.
 *
 * @param context ENP runtime context.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 * @return Transport error otherwise.
 */
esp_err_t enp_service_discovery_send(enp_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* ENP_SERVICE_DISCOVERY_H */