idf_component_register(
    SRCS "tmc2209.c" "common.c" "tmc_uart_hal_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES driver board_expansion
)
