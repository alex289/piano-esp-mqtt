#include "app_wifi.h"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static esp_timer_handle_t s_reconnect_timer;
static int s_retries;

static void reconnect_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        return;
    }

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        s_retries++;
        if (s_retries > CONFIG_PIANO_WIFI_MAX_RETRY) {
            ESP_LOGE(TAG, "%d failed attempts, restarting", s_retries);
            esp_restart();
        }
        /*
         * Back off via a timer rather than delaying here: this handler runs on
         * the shared event loop task, which must not block.
         */
        const uint64_t delay_us = (s_retries < 5 ? 500 : 5000) * 1000ULL;
        ESP_LOGW(TAG, "disconnected, retry %d in %llu ms", s_retries, delay_us / 1000);
        esp_timer_stop(s_reconnect_timer);
        esp_timer_start_once(s_reconnect_timer, delay_us);
        return;
    }

    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&event->ip_info.ip));
        s_retries = 0;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

esp_err_t app_wifi_start(void)
{
    if (strlen(CONFIG_PIANO_WIFI_SSID) == 0) {
        ESP_LOGE(TAG, "no SSID configured, run 'idf.py menuconfig' -> Piano MQTT Bridge");
        return ESP_ERR_INVALID_STATE;
    }

    s_wifi_events = xEventGroupCreate();

    const esp_timer_create_args_t timer_args = {
        .callback = reconnect_cb,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_cfg = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = { .capable = true, .required = false },
        },
    };
    snprintf((char *)wifi_cfg.sta.ssid, sizeof(wifi_cfg.sta.ssid), "%s", CONFIG_PIANO_WIFI_SSID);
    snprintf((char *)wifi_cfg.sta.password, sizeof(wifi_cfg.sta.password), "%s", CONFIG_PIANO_WIFI_PASSWORD);
    if (strlen(CONFIG_PIANO_WIFI_PASSWORD) == 0) {
        wifi_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    /* The MIDI path is latency sensitive; do not let WiFi sleep between beacons. */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "connecting to \"%s\"", CONFIG_PIANO_WIFI_SSID);
    return ESP_OK;
}

bool app_wifi_wait_connected(int timeout_ms)
{
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT,
                                                 pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}
