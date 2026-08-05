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

#include "../core/protocol/enp_packets.h"
 #include "network/enp_transport_espnow.h"
 #include "esp_log.h"

 #include "core/stats/stats.h"
 #include "../core/enp.h"

 static const char *TAG = "GATEWAY";
 
 typedef struct
 {
     uint32_t packets_received;
     uint32_t packets_sent;

     uint32_t sensor_packets;
     uint32_t ack_sent;

     uint32_t crc_errors;
     uint32_t unknown_packets;

     uint32_t peers_added;
     uint32_t send_failures;
     uint32_t queue_overflows;
 } gateway_stats_t;

 static node_stats_t s_stats;
 
 /*----------------------------------------------------------------------------
  * Receive Callback
  *---------------------------------------------------------------------------*/

 static void enp_gateway_receive(
         const uint8_t *mac,
         const void *data,
         size_t len)
 {
	 enp_stats_inc_rx(&s_stats);
	 
     /* Packet must at least contain a header */
     if (len < sizeof(enp_header_t))
     {
         ESP_LOGW(TAG, "Packet too small");
         return;
     }

     const enp_header_t *header =
             (const enp_header_t *)data;

     /* Basic protocol validation */
     if (header->magic != ENP_MAGIC)
     {
         ESP_LOGW(TAG, "Invalid magic");
         return;
     }

     if (header->version != ENP_PROTOCOL_VERSION)
     {
         ESP_LOGW(TAG, "Unsupported protocol version");
         return;
     }

     switch ((enp_packet_type_t)header->type)
     {
         /*--------------------------------------------------------------
          * SENSOR
          *-------------------------------------------------------------*/

         case ENP_PACKET_SENSOR:
         {
             if (len != sizeof(enp_sensor_packet_t))
             {
                 ESP_LOGW(TAG,
                          "Invalid sensor packet length");
                 return;
             }

             enp_sensor_packet_t packet;

             memcpy(&packet,
                    data,
                    sizeof(packet));

			if (!enp_packet_verify(
			        &packet,
			        sizeof(packet)))
			{
			    ESP_LOGW(TAG, "CRC failed");
				enp_stats_inc_crc_error(&s_stats);
			    return;
			}
			
			enp_stats_inc_rx_sensor(&s_stats);

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

             enp_ack_packet_t ack;

             enp_ack_packet_init(&ack);

             ack.header.sequence = packet.header.sequence;

             ack.acknowledged_sequence =
                     packet.header.sequence;

             ack.status = 0;

			 enp_packet_finalize(
			         &ack,
			         sizeof(ack));

             esp_err_t err =
                 enp_transport_send(mac,
                             &ack,
                             sizeof(ack));

			 if (err == ESP_OK)
			 {
				enp_stats_inc_tx(&s_stats);
			 }
			 else
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

         case ENP_PACKET_ACK:
         {
             if (len != sizeof(enp_ack_packet_t))
             {
                 ESP_LOGW(TAG,
                          "Invalid ACK length");
                 return;
             }

             enp_ack_packet_t ack;
			 
			 enp_stats_inc_tx_ack(&s_stats);

             memcpy(&ack,
                    data,
                    sizeof(ack));

			if (!enp_packet_verify(
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
			 s_stats.unknown_packets++;

             break;
     }
 }

 /*----------------------------------------------------------------------------
  * Public
  *---------------------------------------------------------------------------*/

 esp_err_t enp_gateway_init(void)
 {
     enp_transport_register_receive_callback(
             enp_gateway_receive);

     ESP_LOGI(TAG,
              "Gateway initialized");

     return ESP_OK;
 }
 
 
 void enp_gateway_print_stats(void)
 {
     enp_stats_print(TAG, &s_stats);
 }