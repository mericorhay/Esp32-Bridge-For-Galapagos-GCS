idf_component_register(
    SRCS
        "app_main.cpp"
        "bridge/app_state.cpp"
        "bridge/config.cpp"
        "bridge/demo.cpp"
        "bridge/link_watch.cpp"
        "bridge/state_machine.cpp"
        "bridge/uart_radio.cpp"
        "bridge/udp_relay.cpp"
        "bridge/wifi_ap.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_wifi esp_netif esp_event nvs_flash freertos lwip esp_timer
)

# The SPSC ring and the state-machine table lean on C++20 (span, constexpr
# tables). ESP-IDF 6.x defaults the CXX standard below that, so opt the
# component into 20 explicitly rather than fighting the default per-file.
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
