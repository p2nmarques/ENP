/*
 * enp_address.c
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

/**
 * @file enp_address.c
 *
 * @brief ENP logical network address implementation.
 */

#include "enp_address.h"

/*----------------------------------------------------------
 * Compile-Time Validation
 *---------------------------------------------------------*/

/**
 * ENP addresses are part of the ENP wire representation.
 *
 * Network ID  : 2 bytes
 * Node ID     : 4 bytes
 * Total       : 6 bytes
 */
_Static_assert(sizeof(enp_address_t) == 6U, "Unexpected ENP address size");

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

bool enp_address_equal(const enp_address_t *left, const enp_address_t *right) {
	if ((left == NULL) || (right == NULL)) {
		return false;
	}

	return (left->network == right->network) && (left->node == right->node);
}

bool enp_address_is_broadcast(const enp_address_t *address) {
	if (address == NULL) {
		return false;
	}

	return address->node == ENP_NODE_BROADCAST;
}