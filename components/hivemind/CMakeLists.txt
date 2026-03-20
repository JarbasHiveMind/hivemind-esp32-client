idf_component_register(
    SRCS "src/hivemind.c"
         "src/hivemind_crypto.c"
         "src/hivemind_protocol.c"
         "src/hivemind_binary.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_websocket_client mbedtls cjson esp_timer esp_random log
)
