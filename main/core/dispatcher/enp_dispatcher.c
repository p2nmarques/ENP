/*
 * en_dispatcher.c
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_dispatcher.c
  *
  * @brief ENP packet dispatcher implementation.
  */

  #include "enp_dispatcher.h"

  #include <stdbool.h>
  #include <stddef.h>
  #include <string.h>

  #include "esp_log.h"

 /*----------------------------------------------------------
  * Logging
  *---------------------------------------------------------*/

 static const char *TAG =
         "enp_dispatcher";

 /*----------------------------------------------------------
  * Runtime State
  *---------------------------------------------------------*/

 static enp_context_t *s_context = NULL;

 static const enp_service_t *
         s_services[ENP_DISPATCHER_MAX_SERVICES];

 static size_t s_service_count = 0U;

 static bool s_initialized = false;

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 esp_err_t enp_dispatcher_init(
         enp_context_t *context)
 {
     if (context == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (s_initialized)
     {
         return ESP_ERR_INVALID_STATE;
     }

     s_context = context;

     s_service_count = 0U;

     memset(
             s_services,
             0,
             sizeof(s_services));

     s_initialized = true;

     ESP_LOGI(
             TAG,
             "Dispatcher initialized");

     return ESP_OK;
 }

 /*----------------------------------------------------------
  * Deinitialization
  *---------------------------------------------------------*/

 esp_err_t enp_dispatcher_deinit(void)
 {
     if (!s_initialized)
     {
         return ESP_OK;
     }

     /*
      * The dispatcher does not own the service descriptors.
      */
     memset(
             s_services,
             0,
             sizeof(s_services));

     s_service_count = 0U;

     s_context = NULL;

     s_initialized = false;

     ESP_LOGI(
             TAG,
             "Dispatcher deinitialized");

     return ESP_OK;
 }

 /*----------------------------------------------------------
  * Service Registration
  *---------------------------------------------------------*/

 esp_err_t enp_dispatcher_register(
         const enp_service_t *service)
 {
     if (service == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (!s_initialized)
     {
         return ESP_ERR_INVALID_STATE;
     }

     if ((service->name == NULL) ||
         (service->process == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     /*
      * ENP_PACKET_INVALID is never a valid service type.
      */
     if (service->packet_type ==
         ENP_PACKET_INVALID)
     {
         return ESP_ERR_INVALID_ARG;
     }

     /*
      * Check for duplicate packet-type registration.
      */
     for (size_t index = 0U;
          index < s_service_count;
          ++index)
     {
         if (s_services[index]->packet_type ==
             service->packet_type)
         {
             ESP_LOGE(
                     TAG,
                     "Packet type %u already registered",
                     (unsigned)service->packet_type);

             return ESP_ERR_INVALID_STATE;
         }
     }

     if (s_service_count >=
         ENP_DISPATCHER_MAX_SERVICES)
     {
         return ESP_ERR_NO_MEM;
     }

     /*
      * Initialize the service before making it visible to
      * the dispatcher.
      */
     if (service->init != NULL)
     {
         esp_err_t err =
                 service->init(s_context);

         if (err != ESP_OK)
         {
             ESP_LOGE(
                     TAG,
                     "Service '%s' initialization failed: %s",
                     service->name,
                     esp_err_to_name(err));

             return err;
         }
     }

     s_services[s_service_count++] =
             service;

     ESP_LOGI(
             TAG,
             "Registered service '%s' for packet type %u",
             service->name,
             (unsigned)service->packet_type);

     return ESP_OK;
 }

 /*----------------------------------------------------------
  * Packet Dispatch
  *---------------------------------------------------------*/

 esp_err_t enp_dispatcher_dispatch(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (!s_initialized)
     {
         return ESP_ERR_INVALID_STATE;
     }

     /*
      * A packet must pass complete ENP validation before
      * reaching any service.
      */
     if (!enp_packet_verify(packet))
     {
         ESP_LOGW(
                 TAG,
                 "Rejected invalid ENP packet");

         return ESP_ERR_INVALID_ARG;
     }

     const enp_header_t *header =
             enp_packet_header_const(packet);

     if (header == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     /*
      * Find the service responsible for the packet type.
      */
     for (size_t index = 0U;
          index < s_service_count;
          ++index)
     {
         const enp_service_t *service =
                 s_services[index];

         if (service->packet_type ==
             (enp_packet_type_t)header->type)
         {
             return service->process(
                     s_context,
                     packet);
         }
     }

     ESP_LOGW(
             TAG,
             "No service registered for packet type %u",
             (unsigned)header->type);

     return ESP_ERR_NOT_FOUND;
 }