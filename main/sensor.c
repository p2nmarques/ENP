/*
 * sensor.c
 *
 *  Created on: Aug 4, 2026
 *      Author: Pedro Marques
 *
 * ESP-NOW Sensor Node
 ******************************************************************************/

 #include "sensor.h"

 #include <inttypes.h>
 #include <string.h>

 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"

 #include "esp_log.h"
 #include "esp_err.h"

 #include "espnow.h"
 #include "packets.h"
 #include "utils.h"
 #include "stats.h"

 static const char *TAG = "SENSOR";

 /*------------------------------------------------------------------
  * Configuration
  *-----------------------------------------------------------------*/

 #define SENSOR_TASK_STACK_SIZE    4096
 #define SENSOR_TASK_PRIORITY      5
 #define SENSOR_PERIOD_MS          1000

 /*------------------------------------------------------------------
  * Globals
  *-----------------------------------------------------------------*/

 static uint8_t s_gateway_mac[6];

 static uint32_t s_sequence = 0;
 
 static node_stats_t s_stats;

 /*------------------------------------------------------------------
  * Receive Callback
  *-----------------------------------------------------------------*/

 static void sensor_receive(
         const uint8_t *mac,
         const void *data,
         size_t len)
 {
     (void)mac;

     if (len != sizeof(ack_packet_t))
     {
         return;
     }

     ack_packet_t ack;

     memcpy(&ack, data, sizeof(ack));

     if (!espnow_packet_verify(&ack, sizeof(ack)))
     {
		 stats_inc_crc_error(&s_stats);
		 
		 ESP_LOGW(TAG, "Invalid ACK CRC");
         return;
     }
	 
	 stats_inc_rx(&s_stats);
	 stats_inc_rx_ack(&s_stats);

     ESP_LOGI(TAG,
              "ACK received for sequence %" PRIu32,
              ack.acknowledged_sequence);
 }

 /*------------------------------------------------------------------
  * Sender Task
  *-----------------------------------------------------------------*/

 static void sensor_task(void *arg)
 {
     (void)arg;
	 
	 TickType_t last_stats = xTaskGetTickCount();

     while (true)
     {
         sensor_packet_t packet;

         sensor_packet_init(&packet);

         packet.header.sequence = s_sequence++;

         /*
          * Dummy sensor values
          */

         packet.temperature =
                 20.0f +
                 (float)(packet.header.sequence % 10);

         packet.humidity =
                 50.0f +
                 (float)(packet.header.sequence % 20);

         espnow_packet_finalize(
                 &packet,
                 sizeof(packet));

         esp_err_t err =
             espnow_send(
                 s_gateway_mac,
                 &packet,
                 sizeof(packet));
				 
		 if ((xTaskGetTickCount() - last_stats) >= pdMS_TO_TICKS(10000))
		 {
		     stats_print(TAG, &s_stats);

		     last_stats = xTaskGetTickCount();
		 }

         if (err == ESP_OK)
         {
			 stats_inc_tx(&s_stats);
			 stats_inc_tx_sensor(&s_stats);
			 
             ESP_LOGI(TAG,
                      "TX seq=%" PRIu32
                      " temp=%.1f"
                      " hum=%.1f",
                      packet.header.sequence,
                      packet.temperature,
                      packet.humidity);
         }
         else
         {
             ESP_LOGE(TAG,
                      "Send failed (%s)",
                      esp_err_to_name(err));
         }

         vTaskDelay(
             pdMS_TO_TICKS(
                 SENSOR_PERIOD_MS));
     }
 }

 /*------------------------------------------------------------------
  * Public
  *-----------------------------------------------------------------*/

 esp_err_t sensor_init(void)
 {
     if (!parse_mac_address(
             CONFIG_GATEWAY_MAC,
             s_gateway_mac))
     {
         ESP_LOGE(TAG,
                  "Invalid Gateway MAC");

         return ESP_ERR_INVALID_ARG;
     }

     ESP_LOGI(TAG,
              "Gateway %02X:%02X:%02X:%02X:%02X:%02X",
              s_gateway_mac[0],
              s_gateway_mac[1],
              s_gateway_mac[2],
              s_gateway_mac[3],
              s_gateway_mac[4],
              s_gateway_mac[5]);

     ESP_ERROR_CHECK(
         espnow_add_peer(
             s_gateway_mac));

     espnow_register_receive_callback(
             sensor_receive);
			 
	 stats_init(&s_stats);
			 
     if (xTaskCreate(
             sensor_task,
             "sensor",
             SENSOR_TASK_STACK_SIZE,
             NULL,
             SENSOR_TASK_PRIORITY,
             NULL) != pdPASS)
     {
         return ESP_FAIL;
     }

     ESP_LOGI(TAG,
              "Sensor started");

     return ESP_OK;
 }


