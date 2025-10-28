#include "user_task.h"

void Task1(void *pvParameters)
{
    while(1)
    {

        ESP_LOGI(TAG,"HELLO FROM ESP32!\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}