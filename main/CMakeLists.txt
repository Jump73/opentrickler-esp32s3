idf_component_register(
    SRCS "ot_esp32s3_tmp.c"
    INCLUDE_DIRS "."
    REQUIRES board_expansion motors_mcpwm nvs_flash esp_system log freertos
)

