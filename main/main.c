#include "main.h"

const char *TAG="HQ PRC IoT";

void app_main(void)
{
    relay1_off();
    relay2_off();
    wifi_init();
    configure_uart1();
    xTaskCreate(Task1,"Task1",4096,NULL,5,NULL);
    xTaskCreate(TaskLed,"TaskLed",4096,NULL,5,NULL);
    xTaskCreate(dht11_read_task,"dht11_read_task",4096,NULL,5,NULL);
    xTaskCreate(uart_receive_task,"uart_receive_task",4096,NULL,5,NULL);
}
