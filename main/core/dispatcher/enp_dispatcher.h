/*
 * en_dispatcher.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_dispatcher.h
 *
 * @brief ENP packet dispatcher.
 *
 * The dispatcher validates complete ENP frames and routes
 * them to registered services according to packet type.
 */

#ifndef ENP_DISPATCHER_H
#define ENP_DISPATCHER_H

#include <stddef.h>

#include "esp_err.h"

#include "core/enp_context.h"
#include "core/protocol/enp_packet.h"
#include "core/service/enp_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * Configuration
 *---------------------------------------------------------*/

/**
 * @brief Maximum number of services that can be registered.
 */
#define ENP_DISPATCHER_MAX_SERVICES 16U

/*----------------------------------------------------------
 * Lifecycle
 *---------------------------------------------------------*/

/**
 * @brief Initialize the ENP dispatcher.
 *
 * @param context ENP runtime context.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 */
esp_err_t enp_dispatcher_init(enp_context_t *context);

/**
 * @brief Deinitialize the ENP dispatcher.
 *
 * The dispatcher does not own the registered service
 * descriptors. Service descriptors must remain valid for
 * as long as they are registered.
 *
 * @return ESP_OK on success.
 */
esp_err_t enp_dispatcher_deinit(void);

/*----------------------------------------------------------
 * Service Registration
 *---------------------------------------------------------*/

/**
 * @brief Register an ENP service.
 *
 * A service handles one ENP packet type.
 *
 * Two services cannot register the same packet type.
 *
 * @param service Service descriptor.
 *
 * @return ESP_OK on success.
 * @return ESP_ERR_INVALID_ARG for invalid arguments.
 * @return ESP_ERR_INVALID_STATE if the packet type is
 *         already registered or the dispatcher is not
 *         initialized.
 * @return ESP_ERR_NO_MEM if the service table is full.
 */
esp_err_t enp_dispatcher_register(const enp_service_t *service);

/*----------------------------------------------------------
 * Packet Dispatch
 *---------------------------------------------------------*/

/**
 * @brief Dispatch an ENP packet.
 *
 * The packet is validated before it is passed to a service.
 *
 * @param packet Complete ENP packet.
 * @param source Transport address from which the packet
 *        was received.
 *
 * @return ESP_OK if the packet was processed successfully.
 * @return ESP_ERR_INVALID_ARG if arguments or the packet
 *         are invalid.
 * @return ESP_ERR_INVALID_STATE if the dispatcher is not
 *         initialized.
 * @return ESP_ERR_NOT_FOUND if no service handles the packet.
 * @return Service-specific error otherwise.
 */
esp_err_t enp_dispatcher_dispatch(const enp_packet_t *packet,
								  const enp_transport_address_t *source);

/**
 * @brief Dispatch a validated local ENP packet to its registered service.
 *
 * This entry point is intended for packets that have already been
 * classified and duplicate-suppressed by a higher-level ENP data plane.
 * It validates the packet and invokes the registered service without
 * applying the dispatcher's generic duplicate cache a second time.
 *
 * @param packet Complete ENP packet addressed to the local node.
 * @param source Transport address from which the packet was received.
 *
 * @return ESP_OK if the registered service processed the packet.
 * @return ESP_ERR_INVALID_ARG if arguments or the packet are invalid.
 * @return ESP_ERR_INVALID_STATE if the dispatcher is not initialized.
 * @return ESP_ERR_NOT_FOUND if no service handles the packet.
 * @return Service-specific error otherwise.
 */
esp_err_t enp_dispatcher_dispatch_local(const enp_packet_t *packet,
										const enp_transport_address_t *source);

#ifdef __cplusplus
}
#endif

#endif /* ENP_DISPATCHER_H */