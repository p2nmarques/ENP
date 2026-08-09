# See the build system documentation in IDF programming guide
# for more information about component CMakeLists.txt files.

idf_component_register(
    SRCS
    	"main.c"
        "link/enp_transport_wifi.c"
        "link/enp_transport_espnow.c"
        "application/gateway.c"
        "application/sensor.c"
        "core/enp_address.c"
        "core/enp_context.c"
        "core/enp_transport.c"
        "core/dispatcher/enp_dispatcher.c"
        "core/protocol/enp_packet.c"
        "core/protocol/enp_crc16.c"
        "core/utils/utils.c"
        "core/stats/stats.c"
        "link/enp_transport_espnow.c"
        "link/enp_transport_wifi.c"     # list the source files of this component
    INCLUDE_DIRS  "."      # optional, add here public include directories
    PRIV_INCLUDE_DIRS   # optional, add here private include directories
    REQUIRES 
    	esp_wifi
        esp_event
        esp_netif
        nvs_flash           # optional, list the public requirements (component names)
    PRIV_REQUIRES       # optional, list the private requirements
)
