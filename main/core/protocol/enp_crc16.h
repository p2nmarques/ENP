/*
 * crc16.h
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_crc16.h
  *
  * @brief ENP CRC-16/CCITT-FALSE utility.
  *
  * This module provides the CRC algorithm used by the
  * ENP v0.2 frame integrity mechanism.
  *
  * The CRC implementation is independent of ENP packet
  * structures and may be used by any protocol component
  * that requires the same CRC algorithm.
  */

 #ifndef ENP_CRC16_H
 #define ENP_CRC16_H

 #include <stddef.h>
 #include <stdint.h>

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * CRC-16/CCITT-FALSE
  *---------------------------------------------------------*/

 /**
  * @brief Calculate CRC-16/CCITT-FALSE.
  *
  * Parameters:
  *
  *     Polynomial : 0x1021
  *     Initial    : 0xFFFF
  *     RefIn      : false
  *     RefOut     : false
  *     XorOut     : 0x0000
  *
  * The returned value is the CRC value itself. Serialization
  * of the CRC into an ENP frame is the responsibility of the
  * packet subsystem.
  *
  * @param data Input data.
  * @param length Number of bytes.
  *
  * @return Calculated CRC-16 value.
  *
  * If data is NULL and length is non-zero, zero is returned.
  * A NULL pointer with a length of zero is valid.
  */
 uint16_t enp_crc16(
         const void *data,
         size_t length);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_CRC16_H */