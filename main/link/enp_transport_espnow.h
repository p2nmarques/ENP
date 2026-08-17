/*
 * espnow.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_transport_espnow.h
 *
 * @brief ESP-NOW implementation of the ENP transport interface.
 *
 * Target platform:
 *     ESP-IDF 6.0.2
 */

#ifndef ENP_TRANSPORT_ESPNOW_H
#define ENP_TRANSPORT_ESPNOW_H

#include "core/enp_transport.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the ESP-NOW transport instance.
 *
 * The returned object is statically allocated and owned by
 * the ESP-NOW transport module.
 *
 * @return Pointer to the ESP-NOW transport interface.
 */
enp_transport_t *enp_transport_espnow_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ENP_TRANSPORT_ESPNOW_H */