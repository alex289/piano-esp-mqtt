idf_component_register(
    SRCS
        "main.c"
        "app_wifi.c"
        "app_mqtt.c"
        "app_triggers.c"
    INCLUDE_DIRS "."
    REQUIRES
        usb_midi_host
        chord_detect
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_timer
        mqtt
        json
        esp_app_format
    EMBED_TXTFILES "triggers.json"
)
