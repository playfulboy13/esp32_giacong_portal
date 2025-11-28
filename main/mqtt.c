#include "mqtt.h"

// ==== HTTP ====
httpd_handle_t server = NULL;

// ==== WIFI / EVENT ====
EventGroupHandle_t s_wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int WIFI_FAIL_BIT      = BIT1;

int s_retry_num = 0;

QueueHandle_t mqtt_msg_queue=NULL;
static esp_mqtt_client_handle_t global_client=NULL;

extern const uint8_t hivemq_root_ca_pem_start[] asm("_binary_hivemq_root_ca_pem_start");
extern const uint8_t hivemq_root_ca_pem_stop[] asm("_binary_hivemq_root_ca_pem_stop");

bool wifi_connected=false;
bool mqtt_connected=false;


// ====== NVS: Lưu/đọc SSID/PASS ======
void save_wifi_config(const char *ssid, const char *pass) {
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        ESP_ERROR_CHECK(nvs_set_str(nvs, "ssid", ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs, "pass", pass ? pass : ""));
        ESP_ERROR_CHECK(nvs_commit(nvs));
        nvs_close(nvs);
        ESP_LOGI(TAG, "Saved SSID:'%s' PASS:'%s'", ssid, pass);
    } else {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
    }
}

bool read_wifi_config(char *ssid, size_t ssid_len, char *pass, size_t pass_len) {
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READONLY, &nvs) != ESP_OK) return false;

    size_t need_ssid = ssid_len;
    size_t need_pass = pass_len;
    esp_err_t e1 = nvs_get_str(nvs, "ssid", ssid, &need_ssid);
    esp_err_t e2 = nvs_get_str(nvs, "pass", pass, &need_pass);
    nvs_close(nvs);
    if (e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0') return true;
    return false;
}

void clear_wifi_config(void) {
    nvs_handle_t nvs;
    if (nvs_open("storage", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_key(nvs, "ssid");
        nvs_erase_key(nvs, "pass");
        nvs_commit(nvs);
        nvs_close(nvs);
        ESP_LOGW(TAG, "Cleared WiFi credentials from NVS");
    }
}

// ====== URL decode ======
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            a = src[1]; b = src[2];
            a = (a >= 'a') ? a - 'a' + 10 : (a >= 'A') ? a - 'A' + 10 : a - '0';
            b = (b >= 'a') ? b - 'a' + 10 : (b >= 'A') ? b - 'A' + 10 : b - '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

static void stop_ap_if_connected(void) {
    wifi_mode_t mode;
    esp_wifi_get_mode(&mode);

    if (mode == WIFI_MODE_APSTA) {
        ESP_LOGI(TAG, "STA đã kết nối thành công -> Tắt AP");
        esp_wifi_set_mode(WIFI_MODE_STA);   // chỉ giữ STA
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            if (s_retry_num < MAX_RETRY) {
                s_retry_num++;
                ESP_LOGW(TAG, "STA disconnected. Retry %d/%d…", s_retry_num, MAX_RETRY);
                esp_wifi_connect();
                 wifi_connected=false;
            } else {
                ESP_LOGE(TAG, "Kết nối STA thất bại, vẫn giữ AP để cấu hình lại");
                wifi_connected=false;
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        } else if (event_id == WIFI_EVENT_AP_STACONNECTED) {
            wifi_event_ap_staconnected_t* e = (wifi_event_ap_staconnected_t*) event_data;
            ESP_LOGI(TAG, "Client " MACSTR " joined, AID=%d", MAC2STR(e->mac), e->aid);
            wifi_connected=false;
        } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
            wifi_event_ap_stadisconnected_t* e = (wifi_event_ap_stadisconnected_t*) event_data;
            ESP_LOGI(TAG, "Client " MACSTR " left, AID=%d", MAC2STR(e->mac), e->aid);
            wifi_connected=false;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected=true;
        // ✅ Tắt AP nếu STA đã có IP thành công
        stop_ap_if_connected();
    }
}


// ====== STA connect (blocking wait with timeout) ======
bool wifi_sta_connect_blocking(const char *ssid, const char *pass, uint32_t timeout_ms) {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid)-1);
    strncpy((char*)wifi_config.sta.password, pass ? pass : "", sizeof(wifi_config.sta.password)-1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // vẫn kết nối được mạng open (pass rỗng)
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "STA connected to '%s'", ssid);
        return true;
    }
    ESP_LOGE(TAG, "STA connect timeout/fail");
    esp_wifi_stop();
    esp_wifi_deinit();
    return false;
}

// ====== Start SoftAP + Web server ======
void start_softap(void) {
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASS,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(AP_PASS) == 0) ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP started. SSID:%s PASS:%s", AP_SSID, AP_PASS);
}

// Trạng thái kết nối hiện tại (hiển thị ra WebUI)
static char g_status[64] = "Đang chờ chọn Wi-Fi…";


// ===== HTML =====
static const char *HTML_HEAD =
"<!DOCTYPE html><html lang='vi'><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
"<title>ESP32 Wi-Fi Config</title>"
"<style>"
"body{font-family:system-ui,Arial;background:#f5f7fb;margin:0;padding:16px;}"
".card{max-width:720px;margin:0 auto;background:#fff;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.1);padding:20px;}"
"h1{font-size:22px;margin:0 0 12px;color:#222}"
"#status{padding:10px;margin:10px 0;border-radius:8px;font-weight:500;}"
"#status.ok{background:#e6f7e9;color:#0a7d24}"
"#status.err{background:#fdecea;color:#a30000}"
"#status.wait{background:#fff8e5;color:#8a6d00}"
".row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}"
"button,a.btn{padding:8px 12px;border:0;border-radius:8px;cursor:pointer;background:#1976d2;color:#fff;text-decoration:none;display:inline-block;}"
"button:active,a.btn:active{opacity:.8}"
"table{width:100%;border-collapse:collapse;margin-top:12px;overflow-x:auto;display:block}"
"th,td{padding:8px;border-bottom:1px solid #eee;text-align:left;white-space:nowrap}"
".ssid{font-weight:600}"
".muted{color:#666;font-size:12px}"
"form.inline{display:inline}"
"input[type=password]{padding:6px;border:1px solid #ddd;border-radius:8px;}"
"@media(max-width:600px){button,a.btn{width:100%;margin-top:6px}td,th{font-size:14px}}"
"</style></head><body><div class='card'>";

static const char *HTML_TAIL =
"<p class='muted'>HQ PRC IoT</p></div></body></html>";


// ===== Handler scan WiFi và render =====
static void render_ap_list(httpd_req_t *req) {
    wifi_scan_config_t scanConf = {0};
    esp_wifi_scan_start(&scanConf, true);

    uint16_t ap_num = 0;
    esp_wifi_scan_get_ap_num(&ap_num);
    if (ap_num > 20) ap_num = 20;

    wifi_ap_record_t *ap_records = calloc(ap_num ? ap_num : 1, sizeof(wifi_ap_record_t));
    if (!ap_records) {
        httpd_resp_sendstr_chunk(req, "<p>Lỗi bộ nhớ khi quét.</p>");
        return;
    }
    esp_wifi_scan_get_ap_records(&ap_num, ap_records);

    httpd_resp_sendstr_chunk(req, "<div class='row'><h1 style='margin-right:auto'>Chọn Wi-Fi</h1>"
                                       "<a class='btn' href='/'>Quét lại</a></div>");

    // Hiển thị trạng thái kết nối
    char status_html[128];
    snprintf(status_html, sizeof(status_html),
             "<div id='status' class='%s'>%s</div>",
             strstr(g_status, "✅") ? "ok" :
             strstr(g_status, "❌") ? "err" : "wait",
             g_status);
    httpd_resp_sendstr_chunk(req, status_html);

    httpd_resp_sendstr_chunk(req, "<table><thead><tr>"
                                   "<th>SSID</th><th>RSSI</th><th>Mã hoá</th><th>Kết nối</th>"
                                   "</tr></thead><tbody>");

    for (int i = 0; i < ap_num; i++) {
        char row[1024];
        const char *enc =
            (ap_records[i].authmode == WIFI_AUTH_OPEN) ? "Open" :
            (ap_records[i].authmode == WIFI_AUTH_WEP) ? "WEP" :
            (ap_records[i].authmode == WIFI_AUTH_WPA_PSK) ? "WPA" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_PSK) ? "WPA2" :
            (ap_records[i].authmode == WIFI_AUTH_WPA_WPA2_PSK) ? "WPA/WPA2" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_ENTERPRISE) ? "WPA2-Ent" :
            (ap_records[i].authmode == WIFI_AUTH_WPA3_PSK) ? "WPA3" :
            (ap_records[i].authmode == WIFI_AUTH_WPA2_WPA3_PSK) ? "WPA2/WPA3" : "Other";

        snprintf(row, sizeof(row),
            "<tr>"
            "<td class='ssid'>%s</td>"
            "<td>%d dBm</td>"
            "<td>%s</td>"
            "<td>"
              "<form class='inline' action='/wifi' method='post'>"
                "<input type='hidden' name='ssid' value='%s'>"
                "<input type='password' name='pass' placeholder='Mật khẩu%s'> "
                "<button type='submit'>Kết nối</button>"
              "</form>"
            "</td>"
            "</tr>",
            (char*)ap_records[i].ssid,
            ap_records[i].rssi,
            enc,
            (char*)ap_records[i].ssid,
            (ap_records[i].authmode == WIFI_AUTH_OPEN ? " (mạng mở, để trống)" : "")
        );
        httpd_resp_sendstr_chunk(req, row);
    }
    httpd_resp_sendstr_chunk(req, "</tbody></table>");
    free(ap_records);
}


// ====== HTTP Handlers ======
static esp_err_t root_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    render_ap_list(req);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req) {
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t select_get_handler(httpd_req_t *req) {
    char buf[128]; char ssid[33] = {0};
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req, HTML_HEAD);
    char page[1024];
    snprintf(page, sizeof(page),
        "<h1>Nhập mật khẩu cho: <span class='ssid'>%s</span></h1>"
        "<form action='/wifi' method='post'>"
        "<input type='hidden' name='ssid' value='%s'>"
        "<p><input type='password' name='pass' placeholder='Mật khẩu (mạng mở để trống)'></p>"
        "<p><button type='submit'>Lưu & Reboot</button></p>"
        "</form>"
        "<p><a class='btn' href='/'>Quay lại</a></p>",
        ssid, ssid);
    httpd_resp_sendstr_chunk(req, page);
    httpd_resp_sendstr_chunk(req, HTML_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define MAX_POST_LEN 256
static esp_err_t wifi_post_handler(httpd_req_t *req) {
    if (req->content_len >= MAX_POST_LEN) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Post data too large");
        return ESP_FAIL;
    }
    char buf[MAX_POST_LEN+1];
    int cur = 0;
    while (cur < req->content_len) {
        int r = httpd_req_recv(req, buf + cur, req->content_len - cur);
        if (r <= 0) return ESP_FAIL;
        cur += r;
    }
    buf[cur] = '\0';

    // Parse "ssid=...&pass=..."
    char raw_ssid[64] = {0}, raw_pass[128] = {0};
    // Rộng tay để chịu các ký tự '=' trong pass: tách thủ công
    char *p_ssid = strstr(buf, "ssid=");
    char *p_pass = strstr(buf, "&pass=");
    if (!p_ssid) { httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ssid"); return ESP_FAIL; }

    if (p_pass) {
        size_t len_ssid = (size_t)(p_pass - (p_ssid + 5));
        if (len_ssid >= sizeof(raw_ssid)) len_ssid = sizeof(raw_ssid) - 1;
        memcpy(raw_ssid, p_ssid + 5, len_ssid);
        strncpy(raw_pass, p_pass + 6, sizeof(raw_pass)-1);
    } else {
        strncpy(raw_ssid, p_ssid + 5, sizeof(raw_ssid)-1);
        raw_pass[0] = '\0';
    }

    char ssid[33], pass[65];
    url_decode(ssid, raw_ssid);
    url_decode(pass, raw_pass);

    save_wifi_config(ssid, pass);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
        "<html><body><h1>Đã lưu Wi-Fi. Thiết bị sẽ khởi động lại…</h1>"
        "<p>Nếu không tự kết nối được, ESP32 sẽ quay lại chế độ AP để cấu hình.</p>"
        "</body></html>");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// ====== Webserver start ======
void start_webserver(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.lru_purge_enable = true;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t root_uri    = { .uri = "/",          .method = HTTP_GET,  .handler = root_get_handler    };
        httpd_uri_t select_uri  = { .uri = "/select",    .method = HTTP_GET,  .handler = select_get_handler  };
        httpd_uri_t wifi_uri    = { .uri = "/wifi",      .method = HTTP_POST, .handler = wifi_post_handler    };
        httpd_uri_t favicon_uri = { .uri = "/favicon.ico", .method = HTTP_GET, .handler = favicon_get_handler };

        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &select_uri);
        httpd_register_uri_handler(server, &wifi_uri);
        httpd_register_uri_handler(server, &favicon_uri);
        ESP_LOGI(TAG, "HTTP server started");
    } else {
        ESP_LOGE(TAG, "HTTP server start failed");
    }
}

void check_clear_wifi_config(void) {
    // Cấu hình GPIO34 làm input, pull-up
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << RESET_BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Đọc trạng thái nút
    int level = gpio_get_level(RESET_BTN_GPIO);
    if (level == 0) { // Nhấn nút (kéo xuống thấp)
        ESP_LOGW(TAG, "Reset button pressed → clearing Wi-Fi config...");
        // Xóa toàn bộ NVS (bao gồm Wi-Fi config)
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
}


static void mqtt_event_handler(void *arg,esp_event_base_t event_base,int32_t event_id,void *event_data)
{
    esp_mqtt_event_handle_t event=event_data;
    esp_mqtt_client_handle_t client=event->client;

    switch(event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
        {
            global_client=client;
            esp_mqtt_client_subscribe(client,"namban123/control",1);
            esp_mqtt_client_publish(client,"namban123/status","online",0,0,false);
            mqtt_connected=true;
            break;
        }
        case MQTT_EVENT_DATA:
        {
            mqtt_msg_t msg;
            memset(&msg,0,sizeof(msg));
            snprintf(msg.topic,sizeof(msg.topic),"%.*s",event->topic_len,event->topic);
            snprintf(msg.data,sizeof(msg.data),"%.*s",event->data_len,event->data);
            if(mqtt_msg_queue!=NULL)
            {
                xQueueSendFromISR(mqtt_msg_queue,&msg,0);
            }
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
        {
            mqtt_connected=false;
            break;
        }
        default: 
        {
            ESP_LOGI(TAG,"%d\r\n",event->event_id);
        }
    }
}

void mqtt_app_start(void)
{
    mqtt_msg_queue=xQueueCreate(10,sizeof(mqtt_msg_t));
    esp_mqtt_client_config_t mqtt_config={
        .broker={
            .address.uri="mqtts://1f7d050368244daa8dcc8c94f6887a39.s1.eu.hivemq.cloud",
            .address.port=8883,
            .verification.certificate=(const char*)(hivemq_root_ca_pem_start),
        },
        .credentials={
            .client_id="esp32_client",
            .username="namban_123",
            .authentication.password="Namban123@",
        },
        .session.last_will={
            .topic="namban123/status",
            .msg="offline",
            .qos=1,
            .retain=true,
        },
    };
    esp_mqtt_client_handle_t client=esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_event_handler,NULL);
    esp_mqtt_client_start(client);
    global_client=client;
    
}

static void trim_new_line(char *str)
{
    int len=strlen(str);
    while(len>0&&(str[len-1]=='\r'||str[len-1]=='\n'))
    {
        str[len-1]='\0';
        len--;
    }
}


void wifi_init(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    // Kiểm tra nút reset Wi-Fi
    check_clear_wifi_config();
    // Netif + event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_wifi_event_group = xEventGroupCreate();

    // Nếu có sẵn SSID/PASS → thử vào STA trước, nếu fail → bật AP + portal
    char ssid[33] = {0}, pass[65] = {0};
    bool have_cred = read_wifi_config(ssid, sizeof(ssid), pass, sizeof(pass));

    if (have_cred) {
        ESP_LOGI(TAG, "Found saved Wi-Fi: '%s'", ssid);
        bool ok = wifi_sta_connect_blocking(ssid, pass, STA_CONNECT_TIMEOUT_MS);
        if (ok) {
            //ESP_LOGI(TAG, "Connected as STA. Bạn có thể khởi động MQTT/ứng dụng ở đây.");
            mqtt_app_start();
            xTaskCreate(TaskPublish,"TaskPublish",4096,NULL,5,NULL);
            xTaskCreate(TaskSubScribe,"TaskSubScribe",4096,NULL,5,NULL);
            xTaskCreate(rtc_task,"rtc_task",4096,NULL,5,NULL);
            return;
        } else {
            ESP_LOGW(TAG, "STA connect failed. Fallback to AP portal.");
        }
    } else {
        ESP_LOGW(TAG, "No saved Wi-Fi. Starting AP portal.");
    }

    // Fallback / hoặc lần đầu: AP + portal
    start_softap();
    start_webserver();

}

void beep_on(void)
{
    // Bíp dài
    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(200));
    buzzer_off();
}

void beep_off(void)
{
    // Bíp đôi ngắn
    for(int i=0; i<2; i++)
    {
        buzzer_on();
        vTaskDelay(pdMS_TO_TICKS(80));
        buzzer_off();
        vTaskDelay(pdMS_TO_TICKS(80));
    }
}

void TaskPublish(void *pvParameters)
{
    char buffer[64];
    char room_temp[64];
    char room_humid[64];
    char time_str[64];
    char ds3231_temp[64];
    char wifi_rssi[32];
    uint8_t count = 0;

    wifi_ap_record_t ap_info;

    while (1)
    {
        // --- Lấy RSSI WiFi ---
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        {
            snprintf(wifi_rssi, sizeof(wifi_rssi), "%d", ap_info.rssi);
        }
        else
        {
            snprintf(wifi_rssi, sizeof(wifi_rssi), "N/A");
        }

        // --- Các dữ liệu cũ ---
        snprintf(buffer, sizeof(buffer), "HELLO MQTT, count: %d", count);
        snprintf(room_temp, sizeof(room_temp), "%.1f", (float)temperature / 10);
        snprintf(room_humid, sizeof(room_humid), "%.1f", (float)humidity / 10);
        snprintf(time_str, sizeof(time_str),
                 "Time:%02d:%02d:%02d/T:%02d/Date:%02d-%02d-%04d",
                 rtc_time.hour, rtc_time.min, rtc_time.sec,
                 rtc_time.day_of_week, rtc_time.day, rtc_time.month, rtc_time.year);
        snprintf(ds3231_temp, sizeof(ds3231_temp), "%.1f", temp_ds3231);

        // --- Publish MQTT cũ ---
        esp_mqtt_client_publish(global_client, "namban123/test", buffer, 0, 0, false);
        esp_mqtt_client_publish(global_client, "namban123/room_temp/dht11_temp", room_temp, 0, 0, false);
        esp_mqtt_client_publish(global_client, "namban123/room_humid/dht11_humid", room_humid, 0, 0, false);
        esp_mqtt_client_publish(global_client, "namban123/room_temp/ds3231_temp", ds3231_temp, 0, 0, false);
        esp_mqtt_client_publish(global_client, "namban123/rtc_time", time_str, 0, 0, false);
        esp_mqtt_client_publish(global_client, "namban123/wifi_rssi", wifi_rssi, 0, 0, false);

        // --- Publish dữ liệu từng node dưới dạng JSON ---
        for (int n = 0; n < 3; n++)
        {
            cJSON *root = cJSON_CreateObject();
            cJSON *sensors = cJSON_CreateArray();
            char buf[16];

            for (int i = 0; i < 8; i++)
            {
                snprintf(buf, sizeof(buf), "%.1f", node_data[n].sensor[i]);
                cJSON_AddItemToArray(sensors, cJSON_CreateString(buf));
            }

            cJSON_AddItemToObject(root, "sensor", sensors);
            cJSON_AddNumberToObject(root, "cnt", node_data[n].cnt);
            cJSON_AddNumberToObject(root, "param", node_data[n].param);
            cJSON_AddNumberToObject(root, "rssi", node_data[n].rssi);

            char *json_str = cJSON_PrintUnformatted(root);
            char topic[64];
            snprintf(topic, sizeof(topic), "namban123/node%d", n + 1);
            esp_mqtt_client_publish(global_client, topic, json_str, 0, 1, false);

            cJSON_Delete(root);
            free(json_str);
        }

        // --- Publish trạng thái LORA (fake sensor + lost_rate + total_packets) ---
        cJSON *lora_root = cJSON_CreateObject();
        cJSON *fake_sensors = cJSON_CreateArray();
        char buf[16];

        for (int i = 0; i < FAKE_SENSOR_COUNT; i++)
        {
            snprintf(buf, sizeof(buf), "%.1f", lora_info.fake_sensor[i]);
            cJSON_AddItemToArray(fake_sensors, cJSON_CreateString(buf));
        }

        cJSON_AddItemToObject(lora_root, "fake_sensor", fake_sensors);
        cJSON_AddNumberToObject(lora_root, "lost_rate", lora_info.lost_rate);
        cJSON_AddNumberToObject(lora_root, "total_packets", lora_info.total_packets);

        char *lora_str = cJSON_PrintUnformatted(lora_root);
        esp_mqtt_client_publish(global_client, "namban123/lora_status", lora_str, 0, 1, false);

        cJSON_Delete(lora_root);
        free(lora_str);

        // --- Bộ đếm ---
        count++;
        if (count > 99) count = 0;

        vTaskDelay(pdMS_TO_TICKS(700));
    }
}


void TaskSubScribe(void *pvParameters)
{
    mqtt_msg_t rxBuffer;
    while(1)
    {
        if(xQueueReceive(mqtt_msg_queue,&rxBuffer,portMAX_DELAY)==pdTRUE)
        {
            if(strcmp(rxBuffer.topic,"namban123/control")==0)
            {
                trim_new_line(rxBuffer.data);
                ESP_LOGI(TAG,"%s\r\n",rxBuffer.data);

                if(strcmp(rxBuffer.data,"RELAY1_ON")==0)
                {
                    relay1_on();
                    beep_on();
                    ESP_LOGI(TAG,"RELAY 1 ON\r\n");
                }
                else if(strcmp(rxBuffer.data,"RELAY1_OFF")==0)
                {
                    relay1_off();
                    beep_off();
                    ESP_LOGI(TAG,"RELAY 1 OFF\r\n");
                }

                else if(strcmp(rxBuffer.data,"RELAY2_ON")==0)
                {
                    relay2_on();
                    beep_on();
                    ESP_LOGI(TAG,"RELAY 2 ON\r\n");
                }

                else if(strcmp(rxBuffer.data,"RELAY2_OFF")==0)
                {
                    relay2_off();
                    beep_off();
                    ESP_LOGI(TAG,"RELAY 2 OFF\r\n");
                }
            }
        }
    }
}
