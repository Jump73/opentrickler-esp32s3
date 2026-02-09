idf_component_register(SRCS "neopixel_led.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash charge_mode led_strip)
