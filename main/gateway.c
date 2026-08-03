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

 static gateway_stats_t s_stats = {0};

 /*----------------------------------------------------------------------------
  * Receive Callback
  *---------------------------------------------------------------------------*/

 static void gateway_receive(
         const uint8_t *mac,
         const void *data,
         size_t len)
 {
	 s_stats.packets_received++;
	 
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
				s_stats.crc_errors++;
			    return;
			}
			
			s_stats.sensor_packets++;

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

			 if (err == ESP_OK)
			 {
			     s_stats.ack_sent++;
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

         case ESPNOW_PACKET_ACK:
         {
             if (len != sizeof(ack_packet_t))
             {
                 ESP_LOGW(TAG,
                          "Invalid ACK length");
                 return;
             }

             ack_packet_t ack;
			 
			 s_stats.ack_sent++;

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
			 s_stats.unknown_packets++;

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
 
 
 void gateway_print_stats(void)
 {
     ESP_LOGI(TAG,
              "================ Gateway Statistics ================");

     ESP_LOGI(TAG,
              "Packets Received : %" PRIu32,
              s_stats.packets_received);

     ESP_LOGI(TAG,
              "Sensor Packets   : %" PRIu32,
              s_stats.sensor_packets);

     ESP_LOGI(TAG,
              "ACK Packets      : %" PRIu32,
              s_stats.packets_sent);

     ESP_LOGI(TAG,
              "ACK Sent         : %" PRIu32,
              s_stats.ack_sent);

     ESP_LOGI(TAG,
              "CRC Errors       : %" PRIu32,
              s_stats.crc_errors);

     ESP_LOGI(TAG,
              "Unknown Packets  : %" PRIu32,
              s_stats.unknown_packets);
			  
	 ESP_LOGI(TAG,
	          "Peers Added      : %" PRIu32,
	          s_stats.peers_added);
			  
  	 ESP_LOGI(TAG,
  	          "Send failures    : %" PRIu32,
  	          s_stats.send_failures);
			  
  	 ESP_LOGI(TAG,
	          "Queue Overflows  : %" PRIu32,
	          s_stats.queue_overflows);	  

     ESP_LOGI(TAG,
              "====================================================");
 }