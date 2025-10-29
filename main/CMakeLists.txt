idf_component_register(SRCS "dht11.c" "main.c" "user_task.c" "mqtt.c" "dht11.c"
INCLUDE_DIRS "."
REQUIRES driver freertos mqtt esp_wifi esp_http_server json nvs_flash esp_netif dht
EMBED_TXTFILES "hivemq_root_ca.pem")