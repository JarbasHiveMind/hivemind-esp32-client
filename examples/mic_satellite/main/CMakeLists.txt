idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        hivemind
        driver
        nvs_flash
        esp_event
        esp_netif
        protocol_examples_common
)
