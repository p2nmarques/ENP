/**
 * @file enp_context.c
 *
 * @brief ENP runtime context.
 */

#include "enp_context.h"

#include <string.h>

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

esp_err_t enp_context_init(
        enp_context_t *context,
        const enp_config_t *config,
        const enp_transport_t *transport)
{
    if ((context == NULL) ||
        (config == NULL) ||
        (transport == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    /*------------------------------------------------------
     * Validate transport
     *-----------------------------------------------------*/

    if ((transport->init == NULL) ||
        (transport->deinit == NULL) ||
        (transport->send == NULL) ||
        (transport->set_receive_callback == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(context, 0, sizeof(*context));

    /*------------------------------------------------------
     * Network
     *-----------------------------------------------------*/

    context->network.id = config->network_id;

    context->network.node.id = config->node_id;

    context->network.node.role = config->role;

    context->network.node.next_sequence = 1;

    /*------------------------------------------------------
     * Transport
     *-----------------------------------------------------*/

    context->transport = *transport;

    esp_err_t err =
            enp_transport_init(
                    &context->transport);

    if (err != ESP_OK)
    {
        return err;
    }

    return ESP_OK;
}

esp_err_t enp_context_deinit(
        enp_context_t *context)
{
    if (context == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    return enp_transport_deinit(
            &context->transport);
}