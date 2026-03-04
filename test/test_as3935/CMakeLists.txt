idf_component_register(
    SRCS "test_as3935.c"
    INCLUDE_DIRS "../../main"
    PRIV_REQUIRES
        unity
        json
        esp_timer
)
