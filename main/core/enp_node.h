/*
 * enp_node.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_node.h
 *
 * @brief ENP node runtime representation.
 */

#ifndef ENP_NODE_H
#define ENP_NODE_H

#include <stdint.h>

#include "enp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*----------------------------------------------------------
 * ENP Node
 *---------------------------------------------------------*/

/**
 * @brief ENP node runtime state.
 *
 * This structure represents the identity and basic runtime
 * state of an ENP node.
 *
 * The complete network address of a node is represented by
 * enp_address_t. This structure deliberately stores only
 * the node identifier because the network identifier belongs
 * to the enclosing ENP network.
 */
typedef struct {
	/**
	 * Logical node identifier.
	 */
	enp_node_id_t id;

	/**
	 * Current node role.
	 */
	enp_role_t role;

	/**
	 * Next sequence number to allocate.
	 *
	 * A value of 1 is used as the initial sequence number.
	 */
	enp_sequence_t next_sequence;

} enp_node_t;

#ifdef __cplusplus
}
#endif

#endif /* ENP_NODE_H */