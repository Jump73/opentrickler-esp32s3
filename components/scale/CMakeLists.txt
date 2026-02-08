idf_component_register(SRCS "scale.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash driver board_expansion freertos)
