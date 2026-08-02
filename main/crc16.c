/*
 * crc16.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */


 #include "crc16.h"

 uint16_t crc16_ccitt(
         const void *data,
         size_t length)
 {
     const uint8_t *ptr = data;

     uint16_t crc = 0xFFFF;

     while (length--)
     {
         crc ^= ((uint16_t)*ptr++) << 8;

         for (int i = 0; i < 8; i++)
         {
             if (crc & 0x8000)
             {
                 crc = (crc << 1) ^ 0x1021;
             }
             else
             {
                 crc <<= 1;
             }
         }
     }

     return crc;
 }

