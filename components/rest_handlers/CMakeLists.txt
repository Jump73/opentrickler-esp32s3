idf_component_register(SRCS "rest_handlers.c"
                       INCLUDE_DIRS "include"
                       REQUIRES wifi_manager motors scale charge_mode profile cleanup_mode neopixel_led system_control ui_screens lvgl_port autotune flow_model)
