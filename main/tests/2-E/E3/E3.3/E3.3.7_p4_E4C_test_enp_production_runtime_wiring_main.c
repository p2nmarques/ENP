/*
 * E3.3.7_p4_E4C_test_enp_production_runtime_wiring_main.c
 *
 *  Created on: Aug 17, 2026
 *      Author: Pedro Marques
 *
 * E3.3.7 Phase 4 / P4-E4C
 * Production runtime wiring self-test.
 */
 
 
 #include <stdbool.h>
 #include <string.h>
 #include "core/service/discovery/enp_discovery.h"
 #include "esp_err.h"
 #include "esp_log.h"
 #include "core/enp_context.h"
 #include "core/enp_receive_path.h"
 #include "core/enp_transport.h"
 #include "core/dispatcher/enp_dispatcher.h"
 #include "core/routing/enp_route_table.h"
 #include "core/routing/enp_routing_data_path.h"
 #include "core/service/discovery/enp_service_discovery.h"
 #include "core/protocol/enp_packet.h"
 #include "core/protocol/enp_protocol.h"

 static const char *TAG="E3_3_7_P4_E4C";
 static enp_context_t s_context;
 static enp_route_table_t s_routes;
 static enp_routing_data_path_t s_routing_path;
 static enp_receive_path_t s_receive_path;
 static enp_transport_receive_callback_t s_callback;
 static bool s_transport_init;
 static bool s_transport_deinit;

 static esp_err_t t_init(const enp_config_t *c){(void)c;s_transport_init=true;return ESP_OK;}
 static esp_err_t t_deinit(void){s_transport_deinit=true;return ESP_OK;}
 static esp_err_t t_send(const enp_transport_address_t *d,const void *p,size_t n){(void)d;(void)p;(void)n;return ESP_OK;}
 static esp_err_t t_cb(enp_transport_receive_callback_t cb){s_callback=cb;return ESP_OK;}
 static enp_transport_t s_transport={.init=t_init,.deinit=t_deinit,.send=t_send,.set_receive_callback=t_cb};

 static bool resolve(void *ctx,enp_route_destination_t hop,enp_transport_address_t *addr)
 {
     (void)ctx;(void)hop;
     if(!addr)return false;
     memset(addr,0,sizeof(*addr)); addr->length=6U; addr->value[0]=2U; addr->value[5]=2U; return true;
 }
 static void check(bool ok,const char *msg,bool *all)
 {
     if(ok) ESP_LOGI(TAG,"PASS: %s",msg);
     else {ESP_LOGE(TAG,"FAIL: %s",msg);*all=false;}
 }

 void app_main(void)
 {
     bool all=true;
     ESP_LOGI(TAG,"======================================");
     ESP_LOGI(TAG,"E3.3.7 PHASE 4 / P4-E4C PRODUCTION RUNTIME WIRING");
     ESP_LOGI(TAG,"Production bootstrap composition boundary");
     ESP_LOGI(TAG,"Target: ESP-IDF 6.0.2");
     ESP_LOGI(TAG,"======================================");

     const enp_config_t cfg={.network_id=1U,.node_id=1U,.role=ENP_ROLE_GATEWAY};
     check(enp_context_init(&s_context,&s_transport,&cfg)==ESP_OK,"ENP context initialized with controlled transport",&all);
     check(s_transport_init,"controlled transport initialized once",&all);
     check(enp_dispatcher_init(&s_context)==ESP_OK,"dispatcher initialized",&all);
     check(enp_dispatcher_register(enp_service_discovery_get())==ESP_OK,"discovery service registered",&all);
     check(enp_route_table_init(&s_routes),"production route table initialized",&all);
     check(enp_routing_data_path_init(&s_routing_path,&s_routes,s_context.transport,resolve,&s_context),
           "production routing data path initialized",&all);
     check(enp_receive_path_init(&s_receive_path,&s_context,&s_routing_path)==ESP_OK,
           "production receive path initialized",&all);
     check(enp_receive_path_bind(&s_receive_path)==ESP_OK,"production receive path bound",&all);
     check(enp_transport_set_receive_callback(s_context.transport,enp_receive_path_transport_callback)==ESP_OK,
           "transport callback installed through production receive path",&all);
     check(s_callback==enp_receive_path_transport_callback,"controlled transport retains production receive callback",&all);

     enp_packet_t pkt;
     enp_address_t src={.network=1U,.node=2U};
     enp_packet_init(&pkt,ENP_PACKET_DISCOVERY,&src);
     enp_header_t *h=enp_packet_header(&pkt);
     enp_discovery_payload_t *pl=(enp_discovery_payload_t*)enp_packet_payload(&pkt);
     if(h && pl) {
         h->destination.network=1U; h->destination.node=ENP_NODE_BROADCAST;
         h->flags=ENP_FLAG_BROADCAST; h->sequence=1U;
         memset(pl,0,sizeof(*pl)); pl->role=ENP_ROLE_SENSOR;
         check(enp_packet_seal(&pkt,ENP_DISCOVERY_PAYLOAD_SIZE)==ESP_OK,
               "production Discovery frame constructed",&all);
         enp_transport_address_t ts={{2U,0U,0U,0U,0U,2U},6U};
         s_callback(&ts,enp_packet_data_const(&pkt),enp_packet_length(&pkt));
         enp_address_t discovered={.network=1U,.node=2U};
         enp_transport_address_t resolved={0};
         check(enp_neighbor_get_transport_address(&s_context.neighbors,&discovered,&resolved)==ESP_OK,
               "production receive callback delivered Discovery to normal dispatcher path",&all);
         check(resolved.length==6U,"Discovery updated the production neighbor table",&all);
     } else {ESP_LOGE(TAG,"FAIL: could not construct Discovery packet");all=false;}

     check(enp_receive_path_deinit(&s_receive_path)==ESP_OK,"production receive path deinitialized",&all);
     check(enp_dispatcher_deinit()==ESP_OK,"dispatcher deinitialized",&all);
     check(enp_context_deinit(&s_context)==ESP_OK,"ENP context deinitialized",&all);
     check(s_transport_deinit,"controlled transport deinitialized once",&all);

     ESP_LOGI(TAG,"--------------------------------------");
     if(all) ESP_LOGI(TAG,"E3.3.7 Phase 4 / P4-E4C self-test PASS");
     else ESP_LOGE(TAG,"E3.3.7 Phase 4 / P4-E4C self-test FAIL");
     ESP_LOGI(TAG,"======================================");
 }





