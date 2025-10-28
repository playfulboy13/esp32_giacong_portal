#ifndef _USER_TASK_H
#define _USER_TASK_H

#include "main.h"
#include "mqtt.h"

#define DS_PIN 25
#define SH_CP_PIN 26
#define ST_CP_PIN 27

#define DS(x) gpio_set_level(DS_PIN,(x)?(1):(0));
#define SH_CP(x) gpio_set_level(SH_CP_PIN,(x)?(1):(0));
#define ST_CP(x) gpio_set_level(ST_CP_PIN,(x)?(1):(0));

#define XUNG_DICH() {SH_CP(1);SH_CP(0);}
#define XUNG_CHOT() {ST_CP(1);ST_CP(0);}

#define RELAY1_BIT 0
#define RELAY2_BIT 1
#define LED1_BIT 2
#define LED2_BIT 3

extern uint8_t output_state;

void xuat_1_byte(uint8_t data);
void gpio_init_config(void);
void relay1_on(void);
void relay1_off(void);
void relay2_on(void);
void relay2_off(void);
void led1_on(void);
void led1_off(void);
void led2_on(void);
void led2_off(void);
void Task1(void *pvParameters);
void TaskLed(void *pvParameters);

#define LED1_GPIO       2           // Ví dụ LED ở GPIO2
#define UART_PORT       UART_NUM_1  // Dùng UART1
#define UART_TX_PIN     17          // TX = GPIO17
#define UART_RX_PIN     16          // RX = GPIO16


void Task1(void *pvParameters);

#endif