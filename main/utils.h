/*
 * utils.h
 *
 *  Created on: Aug 4, 2026
 *      Author: Pedro Marques
 */

 #ifndef UTILS_H
 #define UTILS_H

 #include <stdbool.h>
 #include <stdint.h>

 bool parse_mac_address(
         const char *text,
         uint8_t mac[6]);

 #endif
