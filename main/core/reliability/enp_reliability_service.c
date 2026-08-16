/*
 * enp_reliability_service.c
 *
 *  Created on: Aug 16, 2026
 *      Author: Pedro Marques
 *
 * enp_reliability_service.c
 *
 * ENP v0.2 — E3.3.7 Phase 2 dispatcher integration.
 * ESP-IDF 6.0.2 compatible.
 */
 
 
 #include "enp_reliability_service.h"

 #include "esp_log.h"

 #include "core/reliability/enp_reliability.h"

 static const char *TAG = "enp_reliability";

 static esp_err_t reliability_service_init(enp_context_t *context)
 {
     (void)context;
     return ESP_OK;
 }

 static esp_err_t reliability_service_process(
         enp_context_t *context,
         const enp_packet_t *packet,
         const enp_transport_address_t *source)
 {
     (void)source;

     if ((context == NULL) || (packet == NULL))
     {
         return ESP_ERR_INVALID_ARG;
     }

     const uint32_t now_ms = enp_context_time_ms(context);

     if (!enp_reliability_process_ack(packet, now_ms))
     {
         /*
          * A dispatcher-level duplicate may already have been removed before
          * this service is called. An unmatched ACK is therefore not itself
          * a dispatcher failure; it is simply not a completion for an active
          * reliability transaction.
          */
         ESP_LOGD(TAG, "ACK did not complete an active reliability transaction");
     }

     return ESP_OK;
 }

 static const enp_service_t s_service = {
     .name = "reliability_ack",
     .packet_type = ENP_PACKET_ACK,
     .init = reliability_service_init,
     .process = reliability_service_process,
 };

 const enp_service_t *enp_reliability_service_get(void)
 {
     return &s_service;
 }




