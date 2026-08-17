/*
 * enp_context.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_context.h
 *
 * @brief ENP runtime context.
 */

#ifndef ENP_CONTEXT_H
#define ENP_CONTEXT_H

#include <stdint.h>

#include "esp_err.h"

#include "config/enp_config.h"
#include "enp_network.h"
#include "enp_transport.h"
#include "network/enp_neighbor.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * ENP Context
 *---------------------------------------------------------*/

/**
 * @brief ENP runtime context.
 *
 * The context contains the runtime state required by the
 * ENP Core.
 *
 * The transport interface is supplied by the application
 * and referenced by the context. The transport implementation
 * itself remains outside the context.
 */
typedef struct {
	/**
	 * Runtime network state.
	 */
	enp_network_t network;

	enp_neighbor_table_t neighbors;

	/**
	 * Active transport interface.
	 *
	 * The context does not own the transport object.
	 */
	enp_transport_t *transport;

} enp_context_t;

/*----------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------*/

/**
 * @brief Initialize the ENP runtime context.
 *
 * This initializes the local ENP network state and initializes
 * the supplied transport using the supplied ENP configuration.
 *
 * @param context ENP context.
 * @param transport Transport interface.
 * @param config ENP configuration.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 * @return Transport initialization error otherwise.
 */
esp_err_t enp_context_init(enp_context_t *context, enp_transport_t *transport,
						   const enp_config_t *config);

/**
 * @brief Deinitialize the ENP runtime context.
 *
 * The active transport is deinitialized if one is associated
 * with the context.
 *
 * @param context ENP context.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for an invalid context.
 * @return Transport deinitialization error otherwise.
 */
esp_err_t enp_context_deinit(enp_context_t *context);

/**
 * @brief Get the current ENP time in milliseconds.
 *
 * The returned value is a monotonic 32-bit millisecond counter.
 *
 * @param context ENP context.
 *
 * @return Current ENP time in milliseconds.
 */
uint32_t enp_context_time_ms(const enp_context_t *context);

#ifdef __cplusplus
}
#endif

#endif /* ENP_CONTEXT_H */