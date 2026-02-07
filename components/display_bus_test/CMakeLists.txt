idf_component_register(
    SRCS "display_bus_test.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver freertos display_st7567 input_encoder ot_pins
)

