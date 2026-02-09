idf_component_register(
    SRCS "ui_screens.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl charge_mode cleanup_mode scale profile wifi_manager system_control
    PRIV_REQUIRES freertos lvgl_port input_encoder
)
