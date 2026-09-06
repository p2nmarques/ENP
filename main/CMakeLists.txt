# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
    	"main.c"
    	#"tests/3-IG/ig_g_8i_test.c"
        "core/enp_address.c"
        "core/enp_context.c"
        "core/enp_duplicate.c"
        "core/enp_maintenance.c"
        "core/enp_receive_path.c"
        "core/enp_transport.c"
        "core/data/enp_data_plane.c"
        "core/dispatcher/enp_dispatcher.c"
        "core/network/enp_neighbor.c"
        "core/protocol/enp_crc16.c"
        "core/protocol/enp_packet.c"
        "core/protocol/payloads/enp_data.c"
        "core/protocol/payloads/enp_routing.c"
        "core/reliability/enp_e5e.c"
        "core/reliability/enp_e5e_integration.c"
        "core/reliability/enp_reliability.c"
        "core/reliability/enp_reliability_service.c"
        "core/routing/enp_routing_data_path.c"
        "core/routing/enp_routing_runtime.c"
        "core/routing/enp_route_failure_coalescer.c"
        "core/routing/enp_route_discovery.c"
        "core/routing/enp_route_metric.c"
        "core/routing/enp_route_repair.c"
        "core/routing/enp_route_repair_adapter.c"
        "core/routing/enp_route_table.c"
        "core/routing/enp_rerr_generation.c"
        "core/routing/enp_rerr_processor.c"
        "core/routing/enp_rerr_recipient_selection.c"
        "core/routing/enp_rrep_processor.c"
        "core/routing/enp_rreq_processor.c"
        "core/service/discovery/enp_service_discovery.c"
        "core/stats/stats.c"
        "core/utils/utils.c"
        "link/enp_transport_espnow.c"
        "link/enp_transport_wifi.c"     # list the source files of this component
    INCLUDE_DIRS  "."      # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES
    	esp_common 
    	esp_wifi
        esp_event
        esp_netif
        nvs_flash           # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
)
