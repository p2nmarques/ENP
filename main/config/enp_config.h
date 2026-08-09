/*
 * enp_config.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_config.h
  *
  * @brief ENP runtime configuration.
  *
  * This file defines the configuration supplied when an ENP
  * runtime context is initialized.
  */

 #ifndef ENP_CONFIG_H
 #define ENP_CONFIG_H

 #include "enp_defaults.h"
 #include "core/enp_types.h"
 #include "enp_version.h"

 #ifdef __cplusplus
 extern "C"
 {
 #endif

 /*----------------------------------------------------------
  * ENP Configuration
  *---------------------------------------------------------*/

 /**
  * @brief ENP runtime configuration.
  *
  * This structure contains the configuration required to
  * initialize the ENP Core and its active transport.
  *
  * The configuration is supplied during initialization and
  * is not owned or modified by the ENP context.
  */
 typedef struct
 {
     /**
      * Logical ENP network identifier.
      */
     enp_network_id_t network_id;

     /**
      * Local ENP node identifier.
      */
     enp_node_id_t node_id;

     /**
      * Role of the local node.
      */
     enp_role_t role;

 } enp_config_t;

 #ifdef __cplusplus
 }
 #endif

 #endif /* ENP_CONFIG_H */