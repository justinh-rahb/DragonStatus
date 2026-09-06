idf_component_register(SRCS "ds_peer.c"
                       INCLUDE_DIRS "include"
                       REQUIRES dc_peer dc_lighting ds_board ds_lighting esp_app_format esp_netif)
