/**
 * @file enp_context.c
 *
 * @brief ENP runtime context implementation.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "enp_context.h"

#include <string.h>

/*----------------------------------------------------------
 * Private Functions
 *---------------------------------------------------------*/

static esp_err_t enp_context_initialize_transport(enp_context_t *context,
												  const enp_config_t *config);

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

esp_err_t enp_context_init(enp_context_t *context, enp_transport_t *transport,
						   const enp_config_t *config) {
	/*------------------------------------------------------
	 * Validate parameters
	 *-----------------------------------------------------*/

	if ((context == NULL) || (transport == NULL) || (config == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	if ((transport->init == NULL) || (transport->deinit == NULL) ||
		(transport->send == NULL) ||
		(transport->set_receive_callback == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	/*------------------------------------------------------
	 * Initialize runtime state
	 *-----------------------------------------------------*/

	memset(context, 0, sizeof(*context));

	context->transport = transport;

	context->network.id = config->network_id;

	context->network.local.id = config->node_id;

	context->network.local.role = config->role;

	context->network.local.next_sequence = 1U;

	esp_err_t err = enp_neighbor_table_init(&context->neighbors);

	if (err != ESP_OK) {
		return err;
	}

	/*------------------------------------------------------
	 * Initialize transport
	 *-----------------------------------------------------*/

	return enp_context_initialize_transport(context, config);
}

esp_err_t enp_context_deinit(enp_context_t *context) {
	if (context == NULL) {
		return ESP_ERR_INVALID_ARG;
	}

	if (context->transport == NULL) {
		return ESP_OK;
	}

	if (context->transport->deinit == NULL) {
		return ESP_ERR_INVALID_STATE;
	}

	return enp_transport_deinit(context->transport);
}

/*----------------------------------------------------------
 * Private Functions
 *---------------------------------------------------------*/

static esp_err_t enp_context_initialize_transport(enp_context_t *context,
												  const enp_config_t *config) {
	if ((context == NULL) || (context->transport == NULL) || (config == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	return enp_transport_init(context->transport, config);
}

uint32_t enp_context_time_ms(const enp_context_t *context) {
	(void)context;

	const TickType_t ticks = xTaskGetTickCount();

	return (uint32_t)(ticks * portTICK_PERIOD_MS);
}