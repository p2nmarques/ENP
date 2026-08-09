/*
 * enp_service_discovery.c
 *
 *  Created on: Aug 9, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_service_discovery.c
  *
  * @brief ENP discovery service implementation.
  */

 #include "enp_service_discovery.h"


 #include "esp_log.h"

 #include "core/service/discovery/enp_discovery.h"

 static const char *TAG =
         "enp_discovery";

 /*----------------------------------------------------------
  * Forward Declarations
  *---------------------------------------------------------*/

 static esp_err_t enp_service_discovery_init(
         enp_context_t *context);

 static esp_err_t enp_service_discovery_process(
         enp_context_t *context,
         const enp_packet_t *packet,
         const enp_transport_address_t *source);

 /*----------------------------------------------------------
  * Service Descriptor
  *---------------------------------------------------------*/

 static const enp_service_t s_discovery_service =
 {
     .name =
         "discovery",

     .packet_type =
         ENP_PACKET_DISCOVERY,

     .init =
         enp_service_discovery_init,

     .process =
         enp_service_discovery_process
 };

 /*----------------------------------------------------------
  * Public API
  *---------------------------------------------------------*/

 const enp_service_t *enp_service_discovery_get(void)
 {
     return &s_discovery_service;
 }

 /*----------------------------------------------------------
  * Initialization
  *---------------------------------------------------------*/

 static esp_err_t enp_service_discovery_init(
         enp_context_t *context)
 {
     if (context == NULL)
     {
         return ESP_ERR_INVALID_ARG;
     }

     ESP_LOGI(
             TAG,
             "Discovery service initialized");

     return ESP_OK;
 }

 /*----------------------------------------------------------
  * Packet Processing
  *---------------------------------------------------------*/

  static esp_err_t enp_service_discovery_process(
          enp_context_t *context,
          const enp_packet_t *packet,
          const enp_transport_address_t *source)
  {
      if ((context == NULL) ||
          (packet == NULL) ||
          (source == NULL))
      {
          return ESP_ERR_INVALID_ARG;
      }

      const enp_header_t *header =
              enp_packet_header_const(packet);

      if (header == NULL)
      {
          return ESP_ERR_INVALID_ARG;
      }

      /*
       * Discovery has a fixed-size payload.
       */
      if (header->payload_length !=
          ENP_DISCOVERY_PAYLOAD_SIZE)
      {
          ESP_LOGW(
                  TAG,
                  "Invalid discovery payload length: %u",
                  (unsigned)header->payload_length);

          return ESP_ERR_INVALID_SIZE;
      }

      const enp_discovery_payload_t *payload =
              (const enp_discovery_payload_t *)
              enp_packet_payload_const(packet);

      if (payload == NULL)
      {
          return ESP_ERR_INVALID_ARG;
      }

      /*
       * Reserved field must be zero in ENP v0.2.
       */
      if (payload->reserved != 0U)
      {
          ESP_LOGW(
                  TAG,
                  "Invalid discovery reserved field");

          return ESP_ERR_INVALID_ARG;
      }

      /*
       * Do not allow a node to advertise the broadcast
       * address as an individual neighbor.
       */
      if (enp_address_is_broadcast(
                  &header->source))
      {
          ESP_LOGW(
                  TAG,
                  "Ignoring discovery from broadcast address");

          return ESP_ERR_INVALID_ARG;
      }

      const uint32_t now_ms =
              enp_context_time_ms(context);

      /*
       * RSSI is not currently exposed by the generic ENP
       * transport callback, so zero means unavailable.
       */
      const int8_t rssi = 0;

      const esp_err_t err =
              enp_neighbor_update(
                      &context->neighbors,
                      &header->source,
                      source,
                      (enp_role_t)payload->role,
                      payload->capabilities,
                      header->sequence,
                      rssi,
                      now_ms);

      if (err != ESP_OK)
      {
          ESP_LOGW(
                  TAG,
                  "Failed to update neighbor: %s",
                  esp_err_to_name(err));

          return err;
      }

      ESP_LOGI(
              TAG,
              "Neighbor discovered: "
              "network=%u "
              "node=%lu "
              "role=%u "
              "capabilities=0x%04X",
              (unsigned)header->source.network,
              (unsigned long)header->source.node,
              (unsigned)payload->role,
              (unsigned)payload->capabilities);

      return ESP_OK;
  }