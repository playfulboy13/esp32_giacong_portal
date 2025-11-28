// lora_uart.h
#ifndef LORA_UART_H
#define LORA_UART_H

#include <stdint.h>
#include <stdbool.h>
#include "main.h"

#define UART1_PORT UART_NUM_1
#define BUF_SIZE   1024
#define UART1_TX_PIN 17
#define UART1_RX_PIN 16
#define XOR_KEY 0x5A
#define HEADER1 0xAA
#define HEADER2 0x55

#define NODE_PAYLOAD_SIZE 34  // 1 CNT + 1 RSSI + 8 float = 34 bytes
#define FAKE_SENSOR_COUNT 5

typedef struct {
    float sensor[8];       // dữ liệu sensor thật STM32
    uint8_t cnt;
    uint8_t param;
    int8_t rssi;
    bool updated;
} node_info_t;

typedef struct {
    float fake_sensor[FAKE_SENSOR_COUNT]; // 5 cảm biến giả lập
    uint8_t lost_rate;   // %
    uint16_t total_packets;
} lora_info_t;

extern node_info_t node_data[3];
extern lora_info_t lora_info;

void configure_uart1(void);
void uart_receive_task(void *arg);
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len);

#endif
