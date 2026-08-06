/*
 * enp_service.h
 *
 *  Created on: Aug 6, 2026
 *      Author: Pedro Marques
 */

#ifndef MAIN_CORE_SERVICE_ENP_SERVICE_H_
#define MAIN_CORE_SERVICE_ENP_SERVICE_H_

#include <stdint.h>

#include "esp_err.h"

#include "/core/enp_context.h"
#include "protocol/enp_packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Maximum packet type supported is 63.
 */
typedef uint64_t enp_packet_mask_t;

#define ENP_PACKET_BIT(type) \
    ((enp_packet_mask_t)1ULL << (type))

typedef struct enp_service
{
    const char *name;

    enp_packet_mask_t packet_mask;

    esp_err_t (*init)(
            enp_context_t *context);

    esp_err_t (*process)(
            enp_context_t *context,
            const enp_packet_t *packet);

} enp_service_t;

#ifdef __cplusplus
}
#endif

#endif
