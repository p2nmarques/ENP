/*
 * enp_discovery.h
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_discovery.h
  *
  * @brief ENP v0.2 discovery payload.
  */

 #ifndef ENP_DISCOVERY_H
 #define ENP_DISCOVERY_H

 #include <stdint.h>

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * Discovery Capabilities
  *---------------------------------------------------------*/

 #define ENP_DISCOVERY_CAP_ROUTING \
         (UINT16_C(1) << 0)

 #define ENP_DISCOVERY_CAP_ROOT \
         (UINT16_C(1) << 1)

 #define ENP_DISCOVERY_CAP_APPLICATION \
         (UINT16_C(1) << 2)

 #define ENP_DISCOVERY_CAP_SENSOR \
         (UINT16_C(1) << 3)

 /*----------------------------------------------------------
  * Discovery Payload
  *---------------------------------------------------------*/

 /**
  * @brief ENP discovery payload.
  *
  * The logical source address is already carried by the
  * ENP header and is therefore not duplicated here.
  *
  * The transport address is transport metadata and is also
  * not part of the ENP wire payload.
  */
 typedef struct __attribute__((packed))
 {
     /**
      * Node role.
      */
     uint8_t role;

     /**
      * Node capability flags.
      */
     uint16_t capabilities;

     /**
      * Reserved for future protocol extensions.
      *
      * Must be zero for ENP v0.2.
      */
     uint8_t reserved;

 } enp_discovery_payload_t;

 #define ENP_DISCOVERY_PAYLOAD_SIZE \
         ((uint16_t)sizeof(enp_discovery_payload_t))

 _Static_assert(
         sizeof(enp_discovery_payload_t) == 4U,
         "Invalid ENP discovery payload size");

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_DISCOVERY_H */