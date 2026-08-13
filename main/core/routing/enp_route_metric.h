/*
 * enp_route_metric.h
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 *
 * ENP v0.2 Route Metric Abstraction — R3-A
 *
 * Initial metric policy:
 *     HOP_COUNT
 *
 * Lower values represent better routes.
 */

 #ifndef ENP_ROUTE_METRIC_H
 #define ENP_ROUTE_METRIC_H

 /* ENP v0.2 Route Metric Abstraction — R3-A
  *
  * Initial metric policy:
  *     HOP_COUNT
  *
  * Lower values represent better routes.
  */

 #include <stdbool.h>
 #include <stdint.h>

 #define ENP_ROUTE_METRIC_MAX_VALUE UINT16_MAX

 typedef enum {
     ENP_ROUTE_METRIC_HOP_COUNT = 1,
 } enp_route_metric_type_t;

 typedef struct {
     enp_route_metric_type_t type;
     uint16_t value;
     bool valid;
 } enp_route_metric_t;

 bool enp_route_metric_init(
     enp_route_metric_t *metric,
     enp_route_metric_type_t type);

 bool enp_route_metric_add_hop(
     enp_route_metric_t *metric);

 /*
  * Returns:
  *   < 0 : lhs is better
  *     0 : equal / both invalid / incompatible types
  *   > 0 : lhs is worse
  *
  * Invalid metrics are worse than valid metrics.
  */
 int enp_route_metric_compare(
     const enp_route_metric_t *lhs,
     const enp_route_metric_t *rhs);

 #endif /* ENP_ROUTE_METRIC_H */
