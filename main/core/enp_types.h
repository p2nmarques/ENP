/*
 * enp_types.h
 *
 *  Created on: Aug 5, 2026
 *      Author: Pedro Marques
 */

#ifndef MAIN_CORE_ENP_TYPES_H_
#define MAIN_CORE_ENP_TYPES_H_

typedef enum
{
    ENP_ROLE_UNKNOWN = 0,

    ENP_ROLE_GATEWAY,

    ENP_ROLE_SENSOR,

    ENP_ROLE_RELAY,

    ENP_ROLE_ROOT,

    ENP_ROLE_MONITOR

} enp_role_t;


typedef enum
{
    ENP_OK = 0,

    ENP_ERROR,

    ENP_TIMEOUT,

    ENP_BUSY

} enp_result_t;

#endif /* MAIN_CORE_ENP_TYPES_H_ */
