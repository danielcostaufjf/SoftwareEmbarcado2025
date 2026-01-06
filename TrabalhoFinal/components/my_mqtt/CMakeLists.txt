idf_component_register(SRCS "my_mqtt.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_event esp_wifi mqtt
                    EMBED_TXTFILES "certs/ca.pem")