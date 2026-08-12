idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES ds_board ds_lighting ds_portal dc_lighting dc_wifi dc_source dc_bambu dc_moonraker dc_evlog nvs_flash app_update
)
