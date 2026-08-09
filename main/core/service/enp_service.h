/*
 * enp_service.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

 /**
  * @file enp_service.h
  *
  * @brief ENP service interface.
  */

  /**
   * @file enp_service.h
   *
   * @brief ENP service interface.
   *
   * A service provides application/protocol functionality for
   * one ENP packet type.
   *
   * Services are transport-independent.
   */

  #ifndef ENP_SERVICE_H
  #define ENP_SERVICE_H

  #include "esp_err.h"

  #include "core/enp_context.h"
  #include "core/protocol/enp_packet.h"

  #ifdef __cplusplus
  extern "C"
  {
  #endif

  /*----------------------------------------------------------
   * Service Callbacks
   *---------------------------------------------------------*/

  /**
   * @brief Service initialization callback.
   *
   * Called when the service is registered with the ENP
   * dispatcher.
   *
   * @param context ENP runtime context.
   *
   * @return ESP_OK on success.
   * @return ESP_ERR_INVALID_ARG for invalid arguments.
   * @return Service-specific error otherwise.
   */
  typedef esp_err_t (*enp_service_init_fn)(
          enp_context_t *context);

  /**
   * @brief Service packet processing callback.
   *
   * The packet has already passed the ENP packet validation
   * performed by the dispatcher.
   *
   * The service must treat the packet as read-only.
   *
   * @param context ENP runtime context.
   * @param packet Validated ENP packet.
   *
   * @return ESP_OK on successful processing.
   * @return Service-specific error otherwise.
   */
  typedef esp_err_t (*enp_service_process_fn)(
          enp_context_t *context,
          const enp_packet_t *packet);

  /*----------------------------------------------------------
   * Service Descriptor
   *---------------------------------------------------------*/

  /**
   * @brief ENP service descriptor.
   *
   * Each service registers itself for one ENP packet type.
   *
   * Example:
   *
   *     static const enp_service_t sensor_service =
   *     {
   *         .name = "sensor",
   *         .packet_type = ENP_PACKET_SENSOR,
   *         .init = sensor_init,
   *         .process = sensor_process
   *     };
   */
  typedef struct
  {
      /**
       * Human-readable service name.
       *
       * Used for diagnostics and logging.
       */
      const char *name;

      /**
       * ENP packet type handled by this service.
       */
      enp_packet_type_t packet_type;

      /**
       * Optional service initialization callback.
       *
       * May be NULL if the service does not require
       * initialization.
       */
      enp_service_init_fn init;

      /**
       * Packet processing callback.
       *
       * Must not be NULL.
       */
      enp_service_process_fn process;

  } enp_service_t;

  #ifdef __cplusplus
  }
  #endif

  #endif /* ENP_SERVICE_H */