/*
 * crc16.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_crc16.c
  *
  * @brief ENP CRC-16/CCITT-FALSE implementation.
  */

 #include "enp_crc16.h"

 /*----------------------------------------------------------
  * Private Constants
  *---------------------------------------------------------*/

 /**
  * @brief CRC-16/CCITT-FALSE polynomial.
  */
 #define ENP_CRC16_POLYNOMIAL    0x1021U

 /**
  * @brief CRC-16/CCITT-FALSE initial value.
  */
 #define ENP_CRC16_INITIAL       0xFFFFU

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 uint16_t enp_crc16(
         const void *data,
         size_t length)
 {
     if ((data == NULL) &&
         (length != 0U))
     {
         return 0U;
     }

     const uint8_t *bytes =
             (const uint8_t *)data;

     uint16_t crc =
             ENP_CRC16_INITIAL;

     for (size_t index = 0U;
          index < length;
          ++index)
     {
         crc ^=
                 (uint16_t)bytes[index] << 8U;

         for (uint8_t bit = 0U;
              bit < 8U;
              ++bit)
         {
             if ((crc & 0x8000U) != 0U)
             {
                 crc =
                         (uint16_t)
                         ((crc << 1U) ^
                          ENP_CRC16_POLYNOMIAL);
             }
             else
             {
                 crc =
                         (uint16_t)
                         (crc << 1U);
             }
         }
     }

     return crc;
 }