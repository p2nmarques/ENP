/*
 * enp_service_discovery.h
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_service_discovery.h
  *
  * @brief ENP discovery service.
  */

 #ifndef ENP_SERVICE_DISCOVERY_H
 #define ENP_SERVICE_DISCOVERY_H

 #include "core/service/enp_service.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /**
  * @brief Get the ENP discovery service descriptor.
  *
  * @return Pointer to the statically allocated discovery
  *         service descriptor.
  */
 const enp_service_t *enp_service_discovery_get(void);

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_SERVICE_DISCOVERY_H */