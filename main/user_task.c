#include "user_task.h"


uint8_t output_state=0x00;

void gpio_init_config(void)
{
    gpio_reset_pin(DS_PIN);
    gpio_set_direction(DS_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(DS_PIN,0);
    
    gpio_reset_pin(SH_CP_PIN);
    gpio_set_direction(SH_CP_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(SH_CP_PIN,0);

    gpio_reset_pin(ST_CP_PIN);
    gpio_set_direction(ST_CP_PIN,GPIO_MODE_OUTPUT);
    gpio_set_level(ST_CP_PIN,0);

    output_state=0x00;
    xuat_1_byte(output_state);
}

void xuat_1_byte(uint8_t data)
{
    for(int i=7;i>=0;i--)
    {
        DS((data>>i)&0x01);
        XUNG_DICH()
    }
    XUNG_CHOT()
}

void relay1_on(void)
{
    output_state|=(1<<RELAY1_BIT);
    xuat_1_byte(output_state);
}
void relay1_off(void)
{
    output_state&=~(1<<RELAY1_BIT);
    xuat_1_byte(output_state);
}

void relay2_on(void)
{
    output_state|=(1<<RELAY2_BIT);
    xuat_1_byte(output_state);
}
void relay2_off(void)
{
    output_state&=~(1<<RELAY2_BIT);
    xuat_1_byte(output_state);
}

void led1_on(void)
{
    output_state|=(1<<LED1_BIT);
    xuat_1_byte(output_state);
}
void led1_off(void)
{
    output_state&=~(1<<LED1_BIT);
    xuat_1_byte(output_state);
}

void led2_on(void)
{
    output_state|=(1<<LED2_BIT);
    xuat_1_byte(output_state);
}
void led2_off(void)
{
    output_state&=~(1<<LED2_BIT);
    xuat_1_byte(output_state);
}

void buzzer_on(void)
{
    output_state|=(1<<BUZZER_BIT);
    xuat_1_byte(output_state);
}
void buzzer_off(void)
{
    output_state&=~(1<<BUZZER_BIT);
    xuat_1_byte(output_state);
}

void uart1_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0);

   
}

void Task1(void *pvParameters)
{
    gpio_init_config();
    uart1_init();

    const char *test_str = "Hello from UART1 (TX=17, RX=16)\r\n";

    while(1)
    {
        //ESP_LOGI(TAG,"ESP32 KIT TEST!\r\n");
        uart_write_bytes(UART_PORT, test_str, strlen(test_str));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void TaskLed(void *pvParameters)
{
    while (1)
    {
        if (wifi_connected == true)
        {
            // --- LED1: báo WiFi ---
            led1_on();
            vTaskDelay(pdMS_TO_TICKS(50));
            led1_off();
            vTaskDelay(pdMS_TO_TICKS(1000));

            // --- LED2: báo MQTT ---
            if (mqtt_connected == true)
            {
                led2_on();
                vTaskDelay(pdMS_TO_TICKS(300));
                led2_off();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            else
            {
                // MQTT chưa kết nối -> LED2 nháy chậm khác kiểu
                led2_on();
                vTaskDelay(pdMS_TO_TICKS(100));
                led2_off();
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
        else
        {
            // WiFi chưa kết nối -> LED1 nháy đều
            led1_on();
            buzzer_on();
            vTaskDelay(pdMS_TO_TICKS(500));
            led1_off();
            buzzer_off();
            vTaskDelay(pdMS_TO_TICKS(500));

            // Khi WiFi chưa có, MQTT chắc chắn cũng chưa dùng được
            led2_off();
        }
    }

}