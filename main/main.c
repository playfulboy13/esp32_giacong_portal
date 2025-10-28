#include "main.h"

const char *TAG="ESP32_KIT";

void app_main(void)
{
    nvs_flash_init();
    
    xTaskCreate(Task1,"Task1",4096,NULL,5,NULL);

}
