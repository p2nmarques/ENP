/*
 * crc16.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 #ifndef CRC16_H
 #define CRC16_H

 #include <stddef.h>
 #include <stdint.h>

 uint16_t enp_crc16_ccitt(
         const void *data,
         size_t length);

 #endif
