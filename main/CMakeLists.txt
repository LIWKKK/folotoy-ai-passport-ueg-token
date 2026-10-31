idf_component_register(
    SRCS "we_app.c"
         "we_f1.c"
         "we_f1_net.c"
         "we_font_16.c"
         "we_font_24.c"
         "we_http.c"
         "we_logic.c"
         "we_net.c"
         "we_nfc.c"
         "we_portal.c"
         "we_sound.c"
         "we_storage.c"
         "we_theme.c"
         "we_time.c"
         "we_wifi.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_event esp_http_client esp_http_server esp_netif esp_timer esp_wifi mbedtls nvs_flash
)
