#ifndef _MQTT_H
#define _MQTT_H

#include "main.h"
#include "user_task.h"
#include "dht11.h"


#define RESET_BTN_GPIO 34

// ==== CẤU HÌNH AP PORTAL ====
#define AP_SSID        "HQPRC_NBN01"
#define AP_PASS        "12345678"
#define AP_CHANNEL     1
#define AP_MAX_CONN    4

// ==== THỜI GIAN CHỜ KẾT NỐI STA (ms) ====
#define STA_CONNECT_TIMEOUT_MS 15000

// ==== HTTP ====
extern httpd_handle_t server;

// ==== WIFI / EVENT ====
extern EventGroupHandle_t s_wifi_event_group;
extern const int WIFI_CONNECTED_BIT;
extern const int WIFI_FAIL_BIT;

extern int s_retry_num;
#define MAX_RETRY 10

#define MAX_MQTT_MSG_LEN 128
typedef struct
{
    char topic[64];
    char data[MAX_MQTT_MSG_LEN];
}mqtt_msg_t;

extern QueueHandle_t mqtt_msg_queue;

extern bool wifi_connected;
extern bool mqtt_connected;

void check_clear_wifi_config(void);
void start_webserver(void);
void start_softap(void);
void clear_wifi_config(void);
bool read_wifi_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
void save_wifi_config(const char *ssid, const char *pass);
bool wifi_sta_connect_blocking(const char *ssid, const char *pass, uint32_t timeout_ms);

void wifi_init(void);

void mqtt_app_start(void);
void TaskPublish(void *pvParameters);
void TaskSubScribe(void *pvParameters);

#endif