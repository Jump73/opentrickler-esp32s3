idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "generated"
    REQUIRES driver esp_timer freertos log nvs_flash spi_flash
        wifi_manager http_server rest_handlers motors scale charge_mode profile cleanup_mode neopixel_led system_control autotune flow_model
        ot_pins display_st7567 lvgl_port ui_screens input_encoder
)
