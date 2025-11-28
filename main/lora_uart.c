#include "lora_uart.h"

#define XOR_KEY 0x5A
#define HEADER1 0xAA
#define HEADER2 0x55
#define NODE_PAYLOAD_SIZE 34  // CNT(1)+RSSI(1)+8*4float=34

node_info_t node_data[3] = {0};
lora_info_t lora_info = {0};

void configure_uart1(void)
{
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // Cấu hình UART
    ESP_ERROR_CHECK(uart_param_config(UART1_PORT, &uart_config));

    // Cấu hình chân TX/RX
    ESP_ERROR_CHECK(uart_set_pin(
        UART1_PORT,
        UART1_TX_PIN,
        UART1_RX_PIN,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    ));

    // Cài driver UART với buffer RX/TX
    ESP_ERROR_CHECK(uart_driver_install(
        UART1_PORT,
        2048,  // RX buffer size
        2048,  // TX buffer size
        0,     // số lượng event queue
        NULL,
        0      // không dùng interrupt queue
    ));

    ESP_LOGI(TAG, "UART1 initialized (TX=%d, RX=%d)", UART1_TX_PIN, UART1_RX_PIN);
}

// CRC16 Modbus
uint16_t crc16_modbus(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for(uint16_t i=0; i<len; i++)
    {
        crc ^= buf[i];
        for(uint8_t j=0; j<8; j++)
        {
            if(crc & 1) crc = (crc >> 1) ^ 0xA001;
            else crc >>= 1;
        }
    }
    return crc;
}

void uart_receive_task(void *arg)
{
    uint8_t buf[BUF_SIZE];
    uint8_t payload[256];

    while(1)
    {
        int len = uart_read_bytes(UART1_PORT, buf, BUF_SIZE, pdMS_TO_TICKS(1000));
        if(len <= 0) continue;

        for(int i = 0; i < len - 4; i++)
        {
            if(buf[i] == HEADER1 && buf[i+1] == HEADER2)
            {
                uint8_t plen = buf[i+2];
                int frame_len = 3 + plen + 2;
                if(i + frame_len > len) continue;

                uint16_t crc_recv = (uint16_t)buf[i+3+plen] | ((uint16_t)buf[i+4+plen] << 8);
                uint16_t crc_calc = crc16_modbus(&buf[i], (uint16_t)(3 + plen));
                if(crc_recv != crc_calc) { ESP_LOGW(TAG, "CRC error"); continue; }

                if((size_t)plen > sizeof(payload)) { ESP_LOGW(TAG, "Payload too large"); continue; }

                for(int k = 0; k < plen; k++)
                    payload[k] = buf[i+3+k] ^ XOR_KEY;

                int offset = 0;

                // --- 3 node ---
                for(int n = 0; n < 3; n++)
                {
                    if(offset + NODE_PAYLOAD_SIZE > plen) break;
                    uint8_t cnt = payload[offset++];
                    int8_t rssi = (int8_t)payload[offset++];
                    float sensors[8];
                    for(int f = 0; f < 8; f++)
                    {
                        memcpy(&sensors[f], &payload[offset], 4);
                        offset += 4;
                    }
                    for(int f = 0; f < 8; f++) node_data[n].sensor[f] = sensors[f];
                    node_data[n].cnt = cnt;
                    node_data[n].rssi = rssi;
                    node_data[n].param = 0;
                    node_data[n].updated = true;

                    ESP_LOGI(TAG, "[NODE %d] S0=%.2f S1=%.2f S2=%.2f S3=%.2f S4=%.2f S5=%.2f S6=%.2f S7=%.2f | CNT=%d | RSSI=%d",
                            n+1,
                            node_data[n].sensor[0], node_data[n].sensor[1], node_data[n].sensor[2], node_data[n].sensor[3],
                            node_data[n].sensor[4], node_data[n].sensor[5], node_data[n].sensor[6], node_data[n].sensor[7],
                            node_data[n].cnt,
                            node_data[n].rssi);
                }

                // --- 5 cảm biến giả lập + thông tin LoRa ---
                if(offset + FAKE_SENSOR_COUNT*4 + 2 <= plen)
                {
                    for(int f = 0; f < FAKE_SENSOR_COUNT; f++)
                    {
                        memcpy(&lora_info.fake_sensor[f], &payload[offset], 4);
                        offset += 4;
                    }
                    lora_info.lost_rate = payload[offset++];
                    uint8_t lo = payload[offset++];
                    uint8_t hi = payload[offset++];
                    lora_info.total_packets = ((uint16_t)hi << 8) | lo;

                    ESP_LOGI(TAG, "[LORA INFO] FakeS0=%.2f S1=%.2f S2=%.2f S3=%.2f S4=%.2f | LostRate=%d%% | Total=%d",
                             lora_info.fake_sensor[0], lora_info.fake_sensor[1], lora_info.fake_sensor[2],
                             lora_info.fake_sensor[3], lora_info.fake_sensor[4],
                             lora_info.lost_rate, lora_info.total_packets);
                }

                i += frame_len - 1;
            }
        }
    }
}

