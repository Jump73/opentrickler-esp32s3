idf_component_register(
    SRCS "ble_commands.c"
    INCLUDE_DIRS "include"
    REQUIRES
        ble_uart
        charge_mode profile scale system_control flow_model
        lvgl_port ui_screens
        json log
)
