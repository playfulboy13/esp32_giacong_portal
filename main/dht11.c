#include "dht11.h"

int16_t humidity;  // Dùng int16_t thay cho float
int16_t temperature;  // Dùng int16_t thay cho float

void dht11_read_task(void *pvParameter)
{
    esp_err_t res;

    while (1)
    {
        res = dht_read_data(DHT_TYPE_DHT11, DHT11_GPIO, &humidity, &temperature);
        if (res == ESP_OK)
        {
            // Chia cho 10 để hiển thị đúng giá trị
            float temp = temperature / 10.0;
            float humi = humidity / 10.0;

            //printf("DHT11: Temp = %.1f°C, Humi = %.1f%%\n", temp, humi);
        }
        else
        {
            printf("DHT11: Failed to read data. err = %d\n", res);
        }

        vTaskDelay(pdMS_TO_TICKS(2000)); // đọc mỗi 2 giây
    }
}