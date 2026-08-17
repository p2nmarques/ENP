/**
 * @file enp_transport.c
 *
 * @brief ENP transport abstraction implementation.
 */

#include "enp_transport.h"

/*----------------------------------------------------------
 * Public API
 *---------------------------------------------------------*/

esp_err_t enp_transport_init(enp_transport_t *transport,
							 const enp_config_t *config) {
	if ((transport == NULL) || (config == NULL) || (transport->init == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	return transport->init(config);
}

esp_err_t enp_transport_deinit(enp_transport_t *transport) {
	if ((transport == NULL) || (transport->deinit == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	return transport->deinit();
}

esp_err_t enp_transport_send(enp_transport_t *transport,
							 const enp_transport_address_t *destination,
							 const void *data, size_t length) {
	if ((transport == NULL) || (transport->send == NULL) ||
		(destination == NULL) || (data == NULL) || (length == 0U)) {
		return ESP_ERR_INVALID_ARG;
	}

	return transport->send(destination, data, length);
}

esp_err_t
enp_transport_set_receive_callback(enp_transport_t *transport,
								   enp_transport_receive_callback_t callback) {
	if ((transport == NULL) || (transport->set_receive_callback == NULL) ||
		(callback == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	return transport->set_receive_callback(callback);
}
esp_err_t enp_transport_set_send_result_callback(
	enp_transport_t *transport, enp_transport_send_result_callback_t callback,
	void *context) {
	if ((transport == NULL) || (transport->set_send_result_callback == NULL) ||
		(callback == NULL)) {
		return ESP_ERR_INVALID_ARG;
	}

	return transport->set_send_result_callback(callback, context);
}
