/**
/**
 * @file enp_transport.c
 *
 * @brief ENP transport wrapper API.
 */

#include "enp_transport.h"

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

esp_err_t enp_transport_init(
        const enp_transport_t *transport)
{
    if ((transport == NULL) ||
        (transport->ops.init == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->ops.init();
}

esp_err_t enp_transport_deinit(
        const enp_transport_t *transport)
{
    if ((transport == NULL) ||
        (transport->ops.deinit == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->ops.deinit();
}

esp_err_t enp_transport_send(
        const enp_transport_t *transport,
        const uint8_t *address,
        const void *data,
        size_t length)
{
    if ((transport == NULL) ||
        (transport->ops.send == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->ops.send(
            address,
            data,
            length);
}

esp_err_t enp_transport_set_receive_callback(
        const enp_transport_t *transport,
        enp_transport_receive_cb_t callback)
{
    if ((transport == NULL) ||
        (transport->ops.set_receive_callback == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    return transport->ops.set_receive_callback(
            callback);
}