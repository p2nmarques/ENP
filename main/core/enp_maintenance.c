/**
 * @file enp_maintenance.c
 *
 * @brief ENP periodic maintenance task implementation.
 */

#include "enp_maintenance.h"

#include <stdbool.h>

#include "config/enp_defaults.h"
#include "core/network/enp_neighbor.h"
#include "core/service/discovery/enp_service_discovery.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define ENP_MAINTENANCE_TASK_STACK_SIZE  3072U
#define ENP_MAINTENANCE_TASK_PRIORITY    4U

static const char *TAG =
        "enp_maintenance";

static enp_context_t *s_context = NULL;
static TaskHandle_t s_task = NULL;
static StaticTask_t s_task_control;
static StackType_t s_task_stack[
        ENP_MAINTENANCE_TASK_STACK_SIZE];
static volatile bool s_running = false;

static void enp_maintenance_task(
        void *argument)
{
    enp_context_t *context =
            (enp_context_t *)argument;

    while (s_running)
    {
        /* A timeout means it is time for the next maintenance
         * cycle. A notification is used only to stop the task
         * promptly during deinitialization. */
        const uint32_t notification =
                ulTaskNotifyTake(
                        pdTRUE,
                        pdMS_TO_TICKS(
                                ENP_DISCOVERY_INTERVAL_MS));

        if (!s_running)
        {
            break;
        }

        if (notification != 0U)
        {
            continue;
        }

        const uint32_t now_ms =
                enp_context_time_ms(context);

        const size_t expired =
                enp_neighbor_expire(
                        &context->neighbors,
                        now_ms,
                        ENP_DISCOVERY_TIMEOUT_MS);

        if (expired > 0U)
        {
            ESP_LOGI(
                    TAG,
                    "Neighbor aging: %u neighbor(s) became STALE",
                    (unsigned)expired);
        }

        const esp_err_t err =
                enp_service_discovery_send(context);

        if (err != ESP_OK)
        {
            ESP_LOGW(
                    TAG,
                    "Periodic discovery failed: %s",
                    esp_err_to_name(err));
        }
        else
        {
            ESP_LOGD(
                    TAG,
                    "Periodic discovery sent");
        }
    }

    s_task = NULL;
    s_context = NULL;
    s_running = false;

    vTaskDelete(NULL);
}

esp_err_t enp_maintenance_init(
        enp_context_t *context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_running || (s_task != NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    s_context = context;
    s_running = true;

    s_task =
            xTaskCreateStatic(
                    enp_maintenance_task,
                    "enp_maint",
                    ENP_MAINTENANCE_TASK_STACK_SIZE,
                    context,
                    ENP_MAINTENANCE_TASK_PRIORITY,
                    s_task_stack,
                    &s_task_control);

    if (s_task == NULL)
    {
        s_context = NULL;
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(
            TAG,
            "Maintenance started: discovery=%u ms timeout=%u ms",
            (unsigned)ENP_DISCOVERY_INTERVAL_MS,
            (unsigned)ENP_DISCOVERY_TIMEOUT_MS);

    return ESP_OK;
}

esp_err_t enp_maintenance_deinit(void)
{
    if (!s_running && (s_task == NULL))
    {
        return ESP_OK;
    }

    s_running = false;

    if (s_task != NULL)
    {
        xTaskNotifyGive(s_task);
    }

    return ESP_OK;
}
