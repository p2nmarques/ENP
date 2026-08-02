/*
 * gateway.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /******************************************************************************
  * gateway.c
  *
  * ESP-NOW Gateway
  ******************************************************************************/

 #include "gateway.h"

 #include <inttypes.h>
 #include <string.h>

 #include "esp_log.h"

 #include "espnow.h"
 #include "packets.h"

 static const char *TAG = "GATEWAY";

 /*----------------------------------------------------------------------------
  * Receive Callback
  *---------------------------------------------------------------------------*/

 static void gateway_receive(
         const uint8_t *mac,
         const void *data,
         size_t len)
 {
     /* Packet must at least contain a header */
     if (len < sizeof(espnow_header_t))
     {
         ESP_LOGW(TAG, "Packet too small");
         return;
     }

     const espnow_header_t *header =
             (const espnow_header_t *)data;

     /* Basic protocol validation */
     if (header->magic != ESPNOW_MAGIC)
     {
         ESP_LOGW(TAG, "Invalid magic");
         return;
     }

     if (header->version != ESPNOW_PROTOCOL_VERSION)
     {
         ESP_LOGW(TAG, "Unsupported protocol version");
         return;
     }

     switch ((espnow_packet_type_t)header->type)
     {
         /*--------------------------------------------------------------
          * SENSOR
          *-------------------------------------------------------------*/

         case ESPNOW_PACKET_SENSOR:
         {
             if (len != sizeof(sensor_packet_t))
             {
                 ESP_LOGW(TAG,
                          "Invalid sensor packet length");
                 return;
             }

             sensor_packet_t packet;

             memcpy(&packet,
                    data,
                    sizeof(packet));

			if (!espnow_packet_verify(
			        &packet,
			        sizeof(packet)))
			{
			    ESP_LOGW(TAG, "CRC failed");
			    return;
			}

             ESP_LOGI(TAG,
                      "Sensor Packet");

             ESP_LOGI(TAG,
                      "Sequence    : %" PRIu32,
                      packet.header.sequence);

             ESP_LOGI(TAG,
                      "Temperature : %.2f C",
                      packet.temperature);

             ESP_LOGI(TAG,
                      "Humidity    : %.2f %%",
                      packet.humidity);

             /* Build ACK */

             ack_packet_t ack;

             ack_packet_init(&ack);

             ack.header.sequence = packet.header.sequence;

             ack.acknowledged_sequence =
                     packet.header.sequence;

             ack.status = 0;

			 espnow_packet_finalize(
			         &ack,
			         sizeof(ack));

             esp_err_t err =
                 espnow_send(mac,
                             &ack,
                             sizeof(ack));

             if (err != ESP_OK)
             {
                 ESP_LOGE(TAG,
                          "ACK send failed (%s)",
                          esp_err_to_name(err));
             }

             break;
         }

         /*--------------------------------------------------------------
          * ACK
          *-------------------------------------------------------------*/

         case ESPNOW_PACKET_ACK:
         {
             if (len != sizeof(ack_packet_t))
             {
                 ESP_LOGW(TAG,
                          "Invalid ACK length");
                 return;
             }

             ack_packet_t ack;

             memcpy(&ack,
                    data,
                    sizeof(ack));

			if (!espnow_packet_verify(
			        &ack,
			        sizeof(ack)))
			{
			    ESP_LOGW(TAG, "ACK CRC failed");
			    return;
			}

             ESP_LOGI(TAG,
                      "ACK received");

             ESP_LOGI(TAG,
                      "Acknowledged Sequence : %" PRIu32,
                      ack.acknowledged_sequence);

             break;
         }

         /*--------------------------------------------------------------
          * Unknown Packet
          *-------------------------------------------------------------*/

         default:

             ESP_LOGW(TAG,
                      "Unknown packet type %u",
                      header->type);

             break;
     }
 }

 /*----------------------------------------------------------------------------
  * Public
  *---------------------------------------------------------------------------*/

 esp_err_t gateway_init(void)
 {
     espnow_register_receive_callback(
             gateway_receive);

     ESP_LOGI(TAG,
              "Gateway initialized");

     return ESP_OK;
 }