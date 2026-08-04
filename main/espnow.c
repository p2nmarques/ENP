/*
 * espnow.c
 *
 *  Created on: Aug 2, 2026
 *      Author: Pedro Marques
 */

 /******************************************************************************
  * espnow.c
  *
  * ESP-IDF 6.0.2
  ******************************************************************************/

 #include "espnow.h"

 #include <string.h>

 #include "esp_log.h"
 #include "esp_now.h"
 #include "esp_wifi.h"

 #define ESPNOW_QUEUE_LENGTH     8
 #define ESPNOW_MAX_DATA_LEN     250
 #define ESPNOW_TASK_STACK       4096
 #define ESPNOW_TASK_PRIORITY    5

 static const char *TAG = "ESPNOW";

 /*-------------------------------------------------------------------------*/
 /* Queue Event                                                             */
 /*-------------------------------------------------------------------------*/

 typedef struct
 {
     uint8_t mac[ESP_NOW_ETH_ALEN];

     size_t len;

     uint8_t data[ESPNOW_MAX_DATA_LEN];

 } espnow_event_t;

 /*-------------------------------------------------------------------------*/
 /* Static Queue                                                            */
 /*-------------------------------------------------------------------------*/

 static StaticQueue_t s_queue_buffer;

 static uint8_t s_queue_storage[
         ESPNOW_QUEUE_LENGTH *
         sizeof(espnow_event_t)];

 static QueueHandle_t s_queue;

 /*-------------------------------------------------------------------------*/
 /* Static Task                                                             */
 /*-------------------------------------------------------------------------*/

 static StackType_t s_task_stack[ESPNOW_TASK_STACK];

 static StaticTask_t s_task_buffer;

 /*-------------------------------------------------------------------------*/
 /* Application Callback                                                    */
 /*-------------------------------------------------------------------------*/

 static espnow_receive_callback_t s_callback = NULL;

 /*-------------------------------------------------------------------------*/
 /* Helper                                                                   */
 /*-------------------------------------------------------------------------*/

 esp_err_t espnow_add_peer(const uint8_t *mac)
 {
     if (esp_now_is_peer_exist(mac))
     {
         return ESP_OK;
     }

     esp_now_peer_info_t peer = {0};

     memcpy(peer.peer_addr,
            mac,
            ESP_NOW_ETH_ALEN);

     peer.ifidx = WIFI_IF_STA;

     /* Follow current WiFi channel */
     peer.channel = 0;

     peer.encrypt = false;

     return esp_now_add_peer(&peer);
 }

 /*-------------------------------------------------------------------------*/
 /* Worker Task                                                             */
 /*-------------------------------------------------------------------------*/

 static void espnow_task(void *arg)
 {
     espnow_event_t event;

     while (1)
     {
         if (xQueueReceive(s_queue,
                           &event,
                           portMAX_DELAY) == pdTRUE)
         {
             espnow_add_peer(event.mac);

             if (s_callback != NULL)
             {
                 s_callback(event.mac,
                            event.data,
                            event.len);
             }
         }
     }
 }

 /*-------------------------------------------------------------------------*/
 /* Receive Callback                                                        */
 /*-------------------------------------------------------------------------*/

 static void espnow_recv_cb(
         const esp_now_recv_info_t *info,
         const uint8_t *data,
         int len)
 {
     if ((info == NULL) ||
         (data == NULL) ||
         (len <= 0))
     {
         return;
     }

     if (len > ESPNOW_MAX_DATA_LEN)
     {
         return;
     }

     espnow_event_t event;

     memcpy(event.mac,
            info->src_addr,
            ESP_NOW_ETH_ALEN);

     event.len = (size_t)len;

     memcpy(event.data,
            data,
            len);

     (void)xQueueSend(s_queue,
                      &event,
                      0);
 }

 /*-------------------------------------------------------------------------*/
 /* Send Callback                                                           */
 /*-------------------------------------------------------------------------*/

 static void espnow_send_cb(
         const esp_now_send_info_t *tx_info,
         esp_now_send_status_t status)
 {
     (void)tx_info;
     (void)status;

     /* Reserved for future logging */
 }

 /*-------------------------------------------------------------------------*/
 /* Public API                                                              */
 /*-------------------------------------------------------------------------*/

 void espnow_register_receive_callback(
         espnow_receive_callback_t callback)
 {
     s_callback = callback;
 }

 esp_err_t espnow_send(
         const uint8_t *mac,
         const void *data,
         size_t len)
 {
     return esp_now_send(mac,
                         (const uint8_t *)data,
                         len);
 }
 
 
 esp_err_t espnow_init(void)
 {
     esp_err_t err;

     s_queue = xQueueCreateStatic(
                     ESPNOW_QUEUE_LENGTH,
                     sizeof(espnow_event_t),
                     s_queue_storage,
                     &s_queue_buffer);

     if (s_queue == NULL)
     {
         return ESP_FAIL;
     }

     if (xTaskCreateStatic(
             espnow_task,
             "espnow",
             ESPNOW_TASK_STACK,
             NULL,
             ESPNOW_TASK_PRIORITY,
             s_task_stack,
             &s_task_buffer) == NULL)
     {
         return ESP_FAIL;
     }

     err = esp_now_init();

     if (err != ESP_OK)
     {
         return err;
     }

     ESP_ERROR_CHECK(
         esp_now_register_recv_cb(
             espnow_recv_cb));

     ESP_ERROR_CHECK(
         esp_now_register_send_cb(
             espnow_send_cb));

     ESP_LOGI(TAG, "ESP-NOW initialized");

     return ESP_OK;
 }


