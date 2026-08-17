/*
 * utils.c
 *
 *  Created on: Aug 4, 2026
 *      Author: Pedro Marques
 */

#include "utils.h"

#include <stdio.h>

bool enp_parse_mac_address(const char *text, uint8_t mac[6]) {
	unsigned int value[6];

	if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &value[0], &value[1], &value[2],
			   &value[3], &value[4], &value[5]) != 6) {
		return false;
	}

	for (int i = 0; i < 6; i++) {
		mac[i] = (uint8_t)value[i];
	}

	return true;
}
