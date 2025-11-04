idf_component_register(SRCS "ds3231.c" "dht11.c" "main.c" "user_task.c" "mqtt.c" "dht11.c" "ds3231.c"
INCLUDE_DIRS "."
REQUIRES driver freertos mqtt esp_wifi esp_http_server json nvs_flash esp_netif dht spiffs
EMBED_TXTFILES "hivemq_root_ca.pem")