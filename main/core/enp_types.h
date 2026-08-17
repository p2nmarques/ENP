/*
 * enp_types.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_types.h
 *
 * @brief Fundamental ENP type definitions.
 *
 * This file defines the primitive types used throughout
 * the ENP Core and Protocol.
 */

#ifndef ENP_TYPES_H
#define ENP_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * Primitive Types
 *---------------------------------------------------------*/

/**
 * @brief ENP node identifier.
 *
 * Size: 32 bits.
 */
typedef uint32_t enp_node_id_t;

/**
 * @brief ENP network identifier.
 *
 * Size: 16 bits.
 */
typedef uint16_t enp_network_id_t;

/**
 * @brief ENP packet sequence number.
 *
 * Size: 32 bits.
 */
typedef uint32_t enp_sequence_t;

/**
 * @brief ENP capability bitmap.
 *
 * Each bit represents a capability supported by a node.
 */
typedef uint32_t enp_capability_t;

/*----------------------------------------------------------
 * Node Roles
 *---------------------------------------------------------*/

/**
 * @brief ENP node role.
 */
typedef enum {
	/**
	 * Node role is not known.
	 */
	ENP_ROLE_UNKNOWN = 0,

	/**
	 * Gateway node.
	 */
	ENP_ROLE_GATEWAY,

	/**
	 * Sensor node.
	 */
	ENP_ROLE_SENSOR,

	/**
	 * Relay node.
	 */
	ENP_ROLE_RELAY,

	/**
	 * Root node.
	 */
	ENP_ROLE_ROOT,

	/**
	 * Monitoring node.
	 */
	ENP_ROLE_MONITOR

} enp_role_t;

#ifdef __cplusplus
}
#endif

#endif /* ENP_TYPES_H */