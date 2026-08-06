/**
 * @file enp_context.c
 *
 * @brief ENP runtime context implementation.
 */

 /**
  * @file enp_context.c
  *
  * @brief ENP runtime context implementation.
  */

 #include "enp_context.h"

 #include <string.h>

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static void initialize_network(
         enp_context_t *context,
         const enp_config_t *config);

 static void initialize_local_node(
         enp_context_t *context,
         const enp_config_t *config);

 static esp_err_t initialize_transport(
         enp_context_t *context);

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 esp_err_t enp_context_init(
         enp_context_t *context,
         const enp_transport_t *transport,
         const enp_config_t *config)
 {
     /*----------------------------------------------------------
      * Validate parameters
      *---------------------------------------------------------*/

     if ((context == NULL) ||
         (transport == NULL) ||
         (config == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     /*----------------------------------------------------------
      * Initialize runtime state
      *---------------------------------------------------------*/

     memset(context, 0, sizeof(*context));

     context->transport = transport;

     initialize_network(
             context,
             config);

     initialize_local_node(
             context,
             config);

     /*----------------------------------------------------------
      * Initialize transport
      *---------------------------------------------------------*/

     return initialize_transport(context);
 }

 esp_err_t enp_context_deinit(
         enp_context_t *context)
 {
     if (context == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     return enp_transport_deinit(
             context->transport);
 }

 /*----------------------------------------------------------
  * Private Functions
  *---------------------------------------------------------*/

 static void initialize_network(
         enp_context_t *context,
         const enp_config_t *config)
 {
     context->network.id = config->network_id;
 }

 static void initialize_local_node(
         enp_context_t *context,
         const enp_config_t *config)
 {
     enp_node_t *local =
             &context->network.local;

     local->id = config->node_id;

     local->role = config->role;

     local->next_sequence = 1;

 #ifdef ENP_CAP_NONE
     local->capabilities = ENP_CAP_NONE;
 #endif
 }

 static esp_err_t initialize_transport(
         enp_context_t *context)
 {
     return enp_transport_init(
             context->transport);
 }}