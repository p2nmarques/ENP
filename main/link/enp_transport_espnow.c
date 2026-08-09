/**
 * @file enp_transport_espnow.c
 *
 * @brief ESP-NOW implementation of the ENP transport interface.
 *
 * Target platform:
 *     ESP-IDF 6.0.2
 */

#include "enp_transport_espnow.h"
#include "core/protocol/enp_protocol.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/*----------------------------------------------------------
 * Configuration
 *---------------------------------------------------------*/

#define ENP_ESPNOW_QUEUE_LENGTH       8U
#define ENP_ESPNOW_TASK_STACK_SIZE    4096U
#define ENP_ESPNOW_TASK_PRIORITY      5U

#define ENP_ESPNOW_MAC_LENGTH         ESP_NOW_ETH_ALEN

/*----------------------------------------------------------
 * Logging
 *---------------------------------------------------------*/

static const char *TAG =
        "enp_espnow";

/*----------------------------------------------------------
 * Queue Events
 *---------------------------------------------------------*/

typedef enum
{
    ENP_ESPNOW_EVENT_STOP = 0,
    ENP_ESPNOW_EVENT_RECEIVE

} enp_espnow_event_type_t;

/**
 * @brief Receive event copied from the ESP-NOW callback.
 */
typedef struct
{
    enp_espnow_event_type_t type;

    enp_transport_address_t source;

    size_t length;

    uint8_t data[ENP_MAX_FRAME_SIZE];

} enp_espnow_event_t;

/*----------------------------------------------------------
 * Static Queue
 *---------------------------------------------------------*/

static StaticQueue_t s_queue_control;

static uint8_t s_queue_storage[
        ENP_ESPNOW_QUEUE_LENGTH *
        sizeof(enp_espnow_event_t)];

static QueueHandle_t s_queue;

/*----------------------------------------------------------
 * Static Task
 *---------------------------------------------------------*/

static StackType_t s_task_stack[
        ENP_ESPNOW_TASK_STACK_SIZE];

static StaticTask_t s_task_control;

static TaskHandle_t s_task;

/*----------------------------------------------------------
 * Runtime State
 *---------------------------------------------------------*/

static bool s_initialized;

static bool s_task_running;

static enp_transport_receive_callback_t
        s_receive_callback;

/*----------------------------------------------------------
 * Forward Declarations
 *---------------------------------------------------------*/

static esp_err_t enp_transport_espnow_init(
        const enp_config_t *config);

static esp_err_t enp_transport_espnow_deinit(void);

static esp_err_t enp_transport_espnow_send(
        const enp_transport_address_t *destination,
        const void *data,
        size_t length);

static esp_err_t enp_transport_espnow_set_receive_callback(
        enp_transport_receive_callback_t callback);

static esp_err_t enp_transport_espnow_add_peer(
        const uint8_t *mac);

static void enp_transport_espnow_task(
        void *argument);

static void enp_transport_espnow_receive_callback(
        const esp_now_recv_info_t *info,
        const uint8_t *data,
        int data_len);

static void enp_transport_espnow_send_callback(
        const esp_now_send_info_t *tx_info,
        esp_now_send_status_t status);

/*----------------------------------------------------------
 * Transport Instance
 *---------------------------------------------------------*/

static enp_transport_t s_transport =
{
    .init =
        enp_transport_espnow_init,

    .deinit =
        enp_transport_espnow_deinit,

    .send =
        enp_transport_espnow_send,

    .set_receive_callback =
        enp_transport_espnow_set_receive_callback
};

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

enp_transport_t *enp_transport_espnow_get(void)
{
    return &s_transport;
}

/*----------------------------------------------------------
 * Initialization
 *---------------------------------------------------------*/

static esp_err_t enp_transport_espnow_init(
        const enp_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Wi-Fi is expected to have already been initialized
     * and started by the application.
     */
    esp_err_t err =
            esp_now_init();

    if (err != ESP_OK)
    {
        ESP_LOGE(
                TAG,
                "esp_now_init failed: %s",
                esp_err_to_name(err));

        return err;
    }

    /*
     * Create the static receive/event queue.
     */
    s_queue =
            xQueueCreateStatic(
                    ENP_ESPNOW_QUEUE_LENGTH,
                    sizeof(enp_espnow_event_t),
                    s_queue_storage,
                    &s_queue_control);

    if (s_queue == NULL)
    {
        ESP_LOGE(
                TAG,
                "Failed to create queue");

        (void)esp_now_deinit();

        return ESP_FAIL;
    }

    /*
     * Create the static worker task.
     */
    s_task_running = true;

    s_task =
            xTaskCreateStatic(
                    enp_transport_espnow_task,
                    "enp_espnow",
                    ENP_ESPNOW_TASK_STACK_SIZE,
                    NULL,
                    ENP_ESPNOW_TASK_PRIORITY,
                    s_task_stack,
                    &s_task_control);

    if (s_task == NULL)
    {
        s_task_running = false;
        s_queue = NULL;

        (void)esp_now_deinit();

        return ESP_FAIL;
    }

    /*
     * Register ESP-NOW receive callback.
     */
    err =
            esp_now_register_recv_cb(
                    enp_transport_espnow_receive_callback);

    if (err != ESP_OK)
    {
        ESP_LOGE(
                TAG,
                "Receive callback registration failed: %s",
                esp_err_to_name(err));

        s_task_running = false;

        vTaskDelete(s_task);

        s_task = NULL;
        s_queue = NULL;

        (void)esp_now_deinit();

        return err;
    }

    /*
     * Register ESP-NOW send callback.
     */
    err =
            esp_now_register_send_cb(
                    enp_transport_espnow_send_callback);

    if (err != ESP_OK)
    {
        ESP_LOGE(
                TAG,
                "Send callback registration failed: %s",
                esp_err_to_name(err));

        (void)esp_now_unregister_recv_cb();

        s_task_running = false;

        vTaskDelete(s_task);

        s_task = NULL;
        s_queue = NULL;

        (void)esp_now_deinit();

        return err;
    }

    s_initialized = true;

    ESP_LOGI(
            TAG,
            "ESP-NOW transport initialized");

    return ESP_OK;
}

/*----------------------------------------------------------
 * Deinitialization
 *---------------------------------------------------------*/

static esp_err_t enp_transport_espnow_deinit(void)
{
    if (!s_initialized)
    {
        return ESP_OK;
    }

    s_receive_callback = NULL;

    /*
     * Stop new receive callbacks.
     */
    esp_err_t err =
            esp_now_unregister_recv_cb();

    if (err != ESP_OK)
    {
        ESP_LOGW(
                TAG,
                "Failed to unregister receive callback: %s",
                esp_err_to_name(err));
    }

    err =
            esp_now_unregister_send_cb();

    if (err != ESP_OK)
    {
        ESP_LOGW(
                TAG,
                "Failed to unregister send callback: %s",
                esp_err_to_name(err));
    }

    /*
     * Ask the worker task to terminate.
     */
    if ((s_queue != NULL) &&
        s_task_running)
    {
        enp_espnow_event_t stop_event =
        {
            .type = ENP_ESPNOW_EVENT_STOP
        };

        if (xQueueSend(
                    s_queue,
                    &stop_event,
                    pdMS_TO_TICKS(100)) != pdTRUE)
        {
            /*
             * The queue may be full. At this stage the
             * transport is already shutting down, so the
             * task can be deleted as a fallback.
             */
            s_task_running = false;

            if (s_task != NULL)
            {
                vTaskDelete(s_task);
            }

            s_task = NULL;
        }
    }

    /*
     * Deinitialize ESP-NOW.
     */
    err =
            esp_now_deinit();

    if (err != ESP_OK)
    {
        return err;
    }

    s_queue = NULL;
    s_task = NULL;
    s_task_running = false;
    s_initialized = false;

    ESP_LOGI(
            TAG,
            "ESP-NOW transport deinitialized");

    return ESP_OK;
}

/*----------------------------------------------------------
 * Send
 *---------------------------------------------------------*/

 static esp_err_t enp_transport_espnow_send(
         const enp_transport_address_t *destination,
         const void *data,
         size_t length)
 {
     if ((destination == NULL) ||
         (data == NULL) ||
         (length == 0U))
     {
         return ESP_ERR_INVALID_ARG;
     }

     if (!s_initialized)
     {
         return ESP_ERR_INVALID_STATE;
     }

     if (length > ENP_MAX_FRAME_SIZE)
     {
         return ESP_ERR_INVALID_SIZE;
     }

     /*
      * Transport broadcast.
      *
      * A zero-length transport address means broadcast.
      */
     if (destination->length == 0U)
     {
         static const uint8_t broadcast_mac[
                 ENP_ESPNOW_MAC_LENGTH] =
         {
             0xFFU,
             0xFFU,
             0xFFU,
             0xFFU,
             0xFFU,
             0xFFU
         };

         return esp_now_send(
                 broadcast_mac,
                 (const uint8_t *)data,
                 length);
     }

     /*
      * Unicast.
      */
     if (destination->length !=
         ENP_ESPNOW_MAC_LENGTH)
     {
         return ESP_ERR_INVALID_ARG;
     }

     esp_err_t err =
             enp_transport_espnow_add_peer(
                     destination->value);

     if (err != ESP_OK)
     {
         return err;
     }

     return esp_now_send(
             destination->value,
             (const uint8_t *)data,
             length);
 }

/*----------------------------------------------------------
 * Receive Callback Registration
 *---------------------------------------------------------*/

static esp_err_t enp_transport_espnow_set_receive_callback(
        enp_transport_receive_callback_t callback)
{
    if (callback == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_receive_callback =
            callback;

    return ESP_OK;
}

/*----------------------------------------------------------
 * Peer Management
 *---------------------------------------------------------*/

static esp_err_t enp_transport_espnow_add_peer(
        const uint8_t *mac)
{
    if (mac == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (esp_now_is_peer_exist(mac))
    {
        return ESP_OK;
    }

    esp_now_peer_info_t peer;

    memset(
            &peer,
            0,
            sizeof(peer));

    memcpy(
            peer.peer_addr,
            mac,
            ESP_NOW_ETH_ALEN);

    peer.ifidx =
            WIFI_IF_STA;

    peer.channel =
            0;

    peer.encrypt =
            false;

    return esp_now_add_peer(
            &peer);
}

/*----------------------------------------------------------
 * Receive Worker
 *---------------------------------------------------------*/

static void enp_transport_espnow_task(
        void *argument)
{
    (void)argument;

    enp_espnow_event_t event;

    while (true)
    {
        if (xQueueReceive(
                    s_queue,
                    &event,
                    portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        if (event.type ==
            ENP_ESPNOW_EVENT_STOP)
        {
            break;
        }

        if (event.type ==
            ENP_ESPNOW_EVENT_RECEIVE)
        {
            enp_transport_receive_callback_t callback =
                    s_receive_callback;

            if (callback != NULL)
            {
                callback(
                        &event.source,
                        event.data,
                        event.length);
            }
        }
    }

    s_task_running = false;

    s_task = NULL;

    vTaskDelete(NULL);
}

/*----------------------------------------------------------
 * ESP-NOW Receive Callback
 *---------------------------------------------------------*/

static void enp_transport_espnow_receive_callback(
        const esp_now_recv_info_t *info,
        const uint8_t *data,
        int data_len)
{
    if ((info == NULL) ||
        (info->src_addr == NULL) ||
        (data == NULL) ||
        (data_len <= 0))
    {
        return;
    }

    if (data_len >
        (int)ENP_MAX_FRAME_SIZE)
    {
        return;
    }

    if (s_queue == NULL)
    {
        return;
    }

    enp_espnow_event_t event;

    memset(
            &event,
            0,
            sizeof(event));

    event.type =
            ENP_ESPNOW_EVENT_RECEIVE;

    memcpy(
            event.source.value,
            info->src_addr,
            ESP_NOW_ETH_ALEN);

    event.source.length =
            ESP_NOW_ETH_ALEN;

    event.length =
            (size_t)data_len;

    memcpy(
            event.data,
            data,
            event.length);

    /*
     * Never block the Wi-Fi task.
     */
    (void)xQueueSend(
            s_queue,
            &event,
            0);
}

/*----------------------------------------------------------
 * ESP-NOW Send Callback
 *---------------------------------------------------------*/

static void enp_transport_espnow_send_callback(
        const esp_now_send_info_t *tx_info,
        esp_now_send_status_t status)
{
    /*
     * Do not perform lengthy processing here.
     *
     * Future TX statistics/retry handling should post
     * an event to a lower-priority task.
     */
    (void)tx_info;
    (void)status;
}