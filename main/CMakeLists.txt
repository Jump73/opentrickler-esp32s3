idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "generated"
    REQUIRES driver esp_timer freertos log nvs_flash spi_flash
        wifi_manager http_server rest_handlers motors scale charge_mode profile cleanup_mode neopixel_led system_control autotune flow_model
        ot_pins display_st7567 lvgl_port ui_screens input_encoder
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    set(HTML_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../html")
    set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
    set(HTML2HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../scripts/html2header.py")

    file(GLOB HTML_FILES "${HTML_DIR}/*.html")

    foreach(HTML_FILE ${HTML_FILES})
        get_filename_component(HTML_NAME "${HTML_FILE}" NAME)
        set(HEADER_FILE "${GENERATED_DIR}/${HTML_NAME}.h")
        add_custom_command(
            OUTPUT "${HEADER_FILE}"
            COMMAND python "${HTML2HEADER}" -f "${HTML_FILE}" -o "${HEADER_FILE}" --no-minify
            DEPENDS "${HTML_FILE}"
            COMMENT "Generating ${HTML_NAME}.h from ${HTML_NAME}"
        )
        list(APPEND GENERATED_HEADERS "${HEADER_FILE}")
    endforeach()

    add_custom_target(html_headers ALL DEPENDS ${GENERATED_HEADERS})
    add_dependencies(${COMPONENT_LIB} html_headers)
endif()
