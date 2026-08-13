/*
 * enp_route_metric.c
 *
 *  Created on: Aug 13, 2026
 *      Author: Pedro Marques
 */

 #include "enp_route_metric.h"

 bool enp_route_metric_init(
     enp_route_metric_t *metric,
     enp_route_metric_type_t type)
 {
     if (metric == NULL) {
         return false;
     }

     metric->type = type;
     metric->value = 0U;
     metric->valid = false;

     if (type != ENP_ROUTE_METRIC_HOP_COUNT) {
         return false;
     }

     metric->valid = true;
     return true;
 }

 bool enp_route_metric_add_hop(
     enp_route_metric_t *metric)
 {
     if (metric == NULL ||
         !metric->valid ||
         metric->type != ENP_ROUTE_METRIC_HOP_COUNT) {
         return false;
     }

     if (metric->value == ENP_ROUTE_METRIC_MAX_VALUE) {
         return false;
     }

     ++metric->value;
     return true;
 }

 int enp_route_metric_compare(
     const enp_route_metric_t *lhs,
     const enp_route_metric_t *rhs)
 {
     if (lhs == NULL && rhs == NULL) {
         return 0;
     }

     if (lhs == NULL) {
         return 1;
     }

     if (rhs == NULL) {
         return -1;
     }

     if (!lhs->valid && !rhs->valid) {
         return 0;
     }

     if (!lhs->valid) {
         return 1;
     }

     if (!rhs->valid) {
         return -1;
     }

     /*
      * Different metric policies are not comparable.
      * The route table must never silently rank unlike metrics.
      */
     if (lhs->type != rhs->type) {
         return 0;
     }

     if (lhs->value < rhs->value) {
         return -1;
     }

     if (lhs->value > rhs->value) {
         return 1;
     }

     return 0;
 }



