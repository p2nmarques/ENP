/*
 * enp_maintenance.h
 *
 *  Created on: Aug 10, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_maintenance.h
 *
 * @brief ENP periodic maintenance task.
 */

#ifndef ENP_MAINTENANCE_H
#define ENP_MAINTENANCE_H

#include "esp_err.h"

#include "core/enp_context.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the ENP periodic maintenance task.
 *
 * The maintenance task periodically:
 *
 * - sends a Discovery announcement;
 * - expires stale neighbors.
 *
 * The task uses statically allocated FreeRTOS resources.
 *
 * @param context ENP runtime context.
 *
 * @return ESP_OK on success.
 */
esp_err_t enp_maintenance_init(enp_context_t *context);

/**
 * @brief Stop the ENP periodic maintenance task.
 *
 * @return ESP_OK on success.
 */
esp_err_t enp_maintenance_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* ENP_MAINTENANCE_H */
