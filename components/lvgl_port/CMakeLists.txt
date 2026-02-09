idf_component_register(
    SRCS "lvgl_port.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl display_st7567 input_encoder
    PRIV_REQUIRES freertos esp_timer driver
)
