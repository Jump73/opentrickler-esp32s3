idf_component_register(
    SRCS "lv_font_unscii_8_prop.c" "lvgl_port.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl display_st7567 input_encoder
    PRIV_REQUIRES freertos esp_timer driver nvs_flash
)
