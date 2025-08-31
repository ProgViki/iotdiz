// ESP32 Door/Alarm IoT — Wi-Fi TCP JSONL client
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_sntp.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_log.h"

#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASS       "YOUR_WIFI_PASSWORD"
#define SERVER_HOST     "192.168.1.50"   // change to your server IP/DNS
#define SERVER_PORT     9000
#define DEVICE_ID       "door-001"
#define DEVICE_TOKEN    "supersecret-token"  // must match server DB/allowlist
#define FW_VERSION      "1.0.0"

// GPIOs
#define GPIO_DOOR       GPIO_NUM_27
#define GPIO_BUZZER     GPIO_NUM_2     // LED/buzzer (active HIGH)
#define DOOR_ACTIVE_LOW 1              // if reed to GND, 1 = open when level LOW

// Timings
#define HEARTBEAT_SEC   30
#define RECONNECT_MS    3000
#define DEBOUNCE_MS     60

static const char *TAG = "iot-door";

// Wi-Fi events
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static volatile int s_door_state = -1;       // 0=closed, 1=open
static int64_t       s_last_edge_ms = 0;

static int64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec/1000;
}

static void gpio_buzz(uint32_t ms) {
    gpio_set_level(GPIO_BUZZER, 1);
    vTaskDelay(pdMS_TO_TICKS(ms));
    gpio_set_level(GPIO_BUZZER, 0);
}

static void sntp_init_time(void) {
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    // wait some seconds for time sync
    for (int i=0;i<10;i++) { vTaskDelay(pdMS_TO_TICKS(500)); }
}

static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Wi-Fi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_LOGI(TAG, "Wi-Fi connected");
    }
}

static void wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wc = {0};
    strncpy((char*)wc.sta.ssid, WIFI_SSID, sizeof(wc.sta.ssid));
    strncpy((char*)wc.sta.password, WIFI_PASS, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static int socket_connect(void) {
    struct sockaddr_in dest = {0};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_HOST, &dest.sin_addr);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct timeval tv = {.tv_sec=5, .tv_usec=0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

static void send_line(int sock, const char *line) {
    size_t len = strlen(line);
    send(sock, line, len, 0);
    send(sock, "\n", 1, 0);
}

static void door_isr(void* arg) {
    int level = gpio_get_level(GPIO_DOOR);
    int64_t t = now_ms();
    if (t - s_last_edge_ms < DEBOUNCE_MS) return;
    s_last_edge_ms = t;

    int open = DOOR_ACTIVE_LOW ? (level == 0) : (level == 1);
    s_door_state = open ? 1 : 0;
}

static void door_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL<<GPIO_DOOR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = DOOR_ACTIVE_LOW ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = DOOR_ACTIVE_LOW ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE
    };
    gpio_config(&io);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(GPIO_DOOR, door_isr, NULL);

    gpio_set_direction(GPIO_BUZZER, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_BUZZER, 0);

    // initialize current state
    int level = gpio_get_level(GPIO_DOOR);
    s_door_state = DOOR_ACTIVE_LOW ? (level==0) : (level==1);
}

static void app_send_event(int sock, const char *type, const char *kv) {
    // Build minimal ISO8601 local time (SNTP set)
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char ts[40];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S%z", &tm);

    char line[256];
    snprintf(line, sizeof(line),
        "{\"type\":\"%s\",\"device_id\":\"%s\",\"ts\":\"%s\"%s}", type, DEVICE_ID, ts, kv);
    send_line(sock, line);
}

static void telemetry_task(void *arg) {
    // wait Wi-Fi
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    sntp_init_time();

    while (1) {
        int sock = -1;
        while (sock < 0) {
            sock = socket_connect();
            if (sock < 0) { vTaskDelay(pdMS_TO_TICKS(RECONNECT_MS)); continue; }
            // HELLO
            char hello[256];
            snprintf(hello, sizeof(hello),
                "{\"type\":\"hello\",\"device_id\":\"%s\",\"token\":\"%s\",\"fw\":\"%s\"}",
                DEVICE_ID, DEVICE_TOKEN, FW_VERSION);
            send_line(sock, hello);
        }

        int last_sent_state = -2;
        int hb_counter = 0;

        while (1) {
            // Send state change if any
            int cur = s_door_state;
            if (cur != last_sent_state && cur != -1) {
                last_sent_state = cur;
                if (cur == 1) {
                    gpio_buzz(60); // chirp on open
                    app_send_event(sock, "door", ",\"state\":\"open\"");
                } else {
                    app_send_event(sock, "door", ",\"state\":\"closed\"");
                }
            }

            // Heartbeat
            if (++hb_counter >= HEARTBEAT_SEC) {
                hb_counter = 0;
                // (Optional) battery or RSSI
                wifi_ap_record_t ap;
                int rssi = (esp_wifi_sta_get_ap_info(&ap)==ESP_OK) ? ap.rssi : 0;
                char kv[64];
                snprintf(kv, sizeof(kv), ",\"rssi\":%d", rssi);
                app_send_event(sock, "hb", kv);
            }

            // read server acks (optional, non-blocking)
            char tmp[32];
            int n = recv(sock, tmp, sizeof(tmp), 0);
            if (n == 0 || (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN)) {
                // disconnected
                close(sock); sock = -1; break;
            }

            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_init();
    door_init();
    xTaskCreate(telemetry_task, "telemetry", 4096, NULL, 5, NULL);
}
