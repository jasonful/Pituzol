idf_component_register(SRCS crypt.c http_helper.c pandora_service.c
                        INCLUDE_DIRS inc
                        REQUIRES esp_http_client json mbedtls)


