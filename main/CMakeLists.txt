idf_component_register(SRCS "main.c" "user_task.c" "mqtt.c"
INCLUDE_DIRS "."
REQUIRES driver freertos mqtt esp_wifi esp_http_server json nvs_flash
EMBED_TXTFILES "hivemq_root_ca.pem")