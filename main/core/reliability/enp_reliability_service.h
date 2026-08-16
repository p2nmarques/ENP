/*
 * enp_reliability_service.h
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 — E3.3.7 Phase 2 dispatcher integration.
 * ESP-IDF 6.0.2 compatible.
 */
  
  
 #ifndef ENP_RELIABILITY_SERVICE_H
 #define ENP_RELIABILITY_SERVICE_H

 #include "core/service/enp_service.h"

 #ifdef __cplusplus
 extern "C" {
 #endif

 /**
  * @brief Return the ENP ACK service descriptor used by the dispatcher.
  *
  * The service forwards validated ACK packets to the E3.3.7 reliability
  * transaction manager. It does not own routing or transport.
  */
 const enp_service_t *enp_reliability_service_get(void);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_RELIABILITY_SERVICE_H */

