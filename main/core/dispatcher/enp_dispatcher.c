/*
 * en_dispatcher.c
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 #include "enp_dispatcher.h"

 #include <stddef.h>

 #include "esp_log.h"

 #define ENP_MAX_SERVICES    16

 static const char *TAG = "ENP_DISPATCHER";

 static enp_context_t *s_context = NULL;

 static const enp_service_t *s_services[ENP_MAX_SERVICES];

 static size_t s_service_count = 0;

 esp_err_t enp_dispatcher_init(
         enp_context_t *context)
 {
     s_context = context;

     s_service_count = 0;

     return ESP_OK;
 }

 esp_err_t enp_dispatcher_register(
         const enp_service_t *service)
 {
     if (service == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (s_service_count >= ENP_MAX_SERVICES)
     {
         return ESP_ERR_NO_MEM;
     }

     s_services[s_service_count++] = service;

     ESP_LOGI(TAG,
              "Registered service: %s",
              service->name);

     if (service->init != NULL)
     {
         return service->init(s_context);
     }

     return ESP_OK;
 }

 esp_err_t enp_dispatcher_dispatch(
         const enp_packet_t *packet)
 {
     if (packet == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     enp_packet_mask_t bit =
         ENP_PACKET_BIT(packet->header.type);

     for (size_t i = 0; i < s_service_count; ++i)
     {
         const enp_service_t *service =
                 s_services[i];

         if (service->packet_mask & bit)
         {
             return service->process(
                     s_context,
                     packet);
         }
     }

     ESP_LOGW(TAG,
              "Unhandled packet type %u",
              packet->header.type);

     return ESP_ERR_NOT_FOUND;
 }