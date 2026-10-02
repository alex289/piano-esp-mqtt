/*
 * Yamaha P-115 -> ESP32-S3 (USB MIDI host) -> MQTT -> Home Assistant.
 *
 * The USB driver callback runs on its own task and must stay short, so MIDI
 * messages are handed to app_task through a queue. Everything that can block
 * (chord detection, MQTT) happens there.
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "app_mqtt.h"
#include "app_triggers.h"
#include "app_wifi.h"
#include "chord_detect.h"
#include "usb_midi_host.h"

static const char *TAG = "piano";

#define MIDI_QUEUE_LEN 64
#define POLL_PERIOD_MS 10

static QueueHandle_t   s_midi_queue;
static app_triggers_t  s_cfg;
static chord_detect_t  s_detect;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* --- USB driver callbacks (USB client task, keep short) ------------------ */

static void on_midi_message(const usb_midi_msg_t *msg, void *arg)
{
    (void)arg;
    /* Dropping on a full queue is correct here: a backlog of stale notes is
     * worse than a gap, and the detector resynchronises on the next chord. */
    xQueueSend(s_midi_queue, msg, 0);
}

static void on_usb_connection(bool connected, const usb_midi_dev_info_t *info, void *arg)
{
    (void)arg;
    if (connected && info) {
        ESP_LOGI(TAG, "piano connected (%04x:%04x)", info->vid, info->pid);
    } else {
        ESP_LOGW(TAG, "piano disconnected");
    }
    app_mqtt_publish_usb(connected);
}

/* --- application task ---------------------------------------------------- */

static void handle_midi(const usb_midi_msg_t *msg, uint32_t t)
{
    switch (msg->status) {
    case MIDI_NOTE_ON:
        chord_detect_note_on(&s_detect, msg->data1, msg->data2, t);
        break;
    case MIDI_NOTE_OFF:
        chord_detect_note_off(&s_detect, msg->data1, t);
        break;
    case MIDI_CONTROL_CHANGE:
        if (msg->data1 == MIDI_CC_SUSTAIN) {
            chord_detect_sustain(&s_detect, msg->data2 >= 64, t);
        } else if (msg->data1 == MIDI_CC_ALL_NOTES_OFF) {
            chord_detect_all_notes_off(&s_detect, t);
        }
        break;
    default:
        break;
    }

#if CONFIG_PIANO_PUBLISH_RAW_MIDI
    app_mqtt_publish_raw_midi(msg);
#endif
}

static void app_task(void *arg)
{
    (void)arg;
    bool playing = false;

    while (true) {
        usb_midi_msg_t msg;
        while (xQueueReceive(s_midi_queue, &msg, pdMS_TO_TICKS(POLL_PERIOD_MS)) == pdTRUE) {
            handle_midi(&msg, now_ms());
        }

        const uint32_t t = now_ms();

        const bool now_playing = (s_detect.held_count > 0);
        if (now_playing != playing) {
            playing = now_playing;
            app_mqtt_publish_playing(playing);
        }

        chord_t chord;
        if (!chord_detect_poll(&s_detect, t, &chord)) {
            continue;
        }
        if (!chord.valid) {
            continue; /* released; the sensor keeps showing the last chord */
        }

        ESP_LOGI(TAG, "chord %-10s (%u notes, vel %u)",
                 chord.name, chord.note_count, chord.velocity);
        app_mqtt_publish_chord(&chord);

        const app_trigger_t *trigger = app_triggers_match(&s_cfg, &chord, t);
        if (trigger) {
            app_mqtt_publish_trigger(trigger, &chord);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(app_triggers_load(&s_cfg));
    chord_detect_init(&s_detect, &s_cfg.detect);

    s_midi_queue = xQueueCreate(MIDI_QUEUE_LEN, sizeof(usb_midi_msg_t));
    ESP_ERROR_CHECK(s_midi_queue ? ESP_OK : ESP_ERR_NO_MEM);

    /*
     * Without credentials the bridge still runs: USB MIDI and chord detection
     * work, and recognised chords show up in the log. That makes it possible
     * to bring up and debug the hardware before any network exists.
     */
    if (app_wifi_start() == ESP_OK) {
        if (!app_wifi_wait_connected(30000)) {
            ESP_LOGW(TAG, "no WiFi yet, carrying on; MQTT will connect when it comes up");
        }
        ESP_ERROR_CHECK(app_mqtt_start(&s_cfg));
    } else {
        ESP_LOGW(TAG, "running offline: chords are logged but not published");
        ESP_LOGW(TAG, "set credentials in credentials.defaults, then rebuild");
    }

    const usb_midi_host_config_t midi_cfg = {
        .on_message    = on_midi_message,
        .on_connection = on_usb_connection,
        .task_priority = 5,
        .task_stack    = 4096,
    };
    ESP_ERROR_CHECK(usb_midi_host_start(&midi_cfg));

    xTaskCreate(app_task, "piano_app", 5120, NULL, 4, NULL);
    ESP_LOGI(TAG, "running");
}
