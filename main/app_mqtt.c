#include "app_mqtt.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

#define BASE   CONFIG_PIANO_MQTT_BASE_TOPIC
#define DEVID  CONFIG_PIANO_DEVICE_ID
#define PREFIX CONFIG_PIANO_HA_DISCOVERY_PREFIX

#define TOPIC_STATUS  BASE "/status"
#define TOPIC_CHORD   BASE "/chord"
#define TOPIC_TRIGGER BASE "/trigger"
#define TOPIC_PLAYING BASE "/playing"
#define TOPIC_USB     BASE "/usb"
#define TOPIC_MIDI    BASE "/midi"

static esp_mqtt_client_handle_t s_client;
static volatile bool            s_connected;
static app_triggers_t           s_triggers;

bool app_mqtt_is_connected(void)
{
    return s_connected;
}

static void publish(const char *topic, const char *payload, int qos, int retain)
{
    if (!s_connected) {
        return;
    }
    esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
}

/* --------------------------------------------------------------------------
 * Home Assistant MQTT discovery
 * ------------------------------------------------------------------------ */

static cJSON *device_block(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    cJSON *dev = cJSON_CreateObject();
    cJSON *ids = cJSON_CreateArray();
    cJSON_AddItemToArray(ids, cJSON_CreateString(DEVID));
    cJSON_AddItemToObject(dev, "identifiers", ids);
    cJSON_AddStringToObject(dev, "name", CONFIG_PIANO_DEVICE_NAME);
    cJSON_AddStringToObject(dev, "manufacturer", "Yamaha");
    cJSON_AddStringToObject(dev, "model", "P-115 (ESP32-S3 USB MIDI bridge)");
    cJSON_AddStringToObject(dev, "sw_version", app->version);
    return dev;
}

static void publish_discovery_doc(const char *topic, cJSON *doc)
{
    char *json = cJSON_PrintUnformatted(doc);
    if (json) {
        esp_mqtt_client_publish(s_client, topic, json, 0, 1, 1 /* retained */);
        free(json);
    }
    cJSON_Delete(doc);
}

static void add_availability(cJSON *doc)
{
    cJSON_AddStringToObject(doc, "availability_topic", TOPIC_STATUS);
    cJSON_AddStringToObject(doc, "payload_available", "online");
    cJSON_AddStringToObject(doc, "payload_not_available", "offline");
}

static void publish_discovery(void)
{
    char topic[192];

    /* Sensor: the chord most recently played. */
    {
        cJSON *doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "name", "Chord");
        cJSON_AddStringToObject(doc, "unique_id", DEVID "_chord");
        cJSON_AddStringToObject(doc, "state_topic", TOPIC_CHORD);
        cJSON_AddStringToObject(doc, "value_template", "{{ value_json.name }}");
        cJSON_AddStringToObject(doc, "json_attributes_topic", TOPIC_CHORD);
        cJSON_AddStringToObject(doc, "icon", "mdi:music-clef-treble");
        add_availability(doc);
        cJSON_AddItemToObject(doc, "device", device_block());
        snprintf(topic, sizeof(topic), "%s/sensor/%s/chord/config", PREFIX, DEVID);
        publish_discovery_doc(topic, doc);
    }

    /* Binary sensor: keys are down right now. */
    {
        cJSON *doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "name", "Playing");
        cJSON_AddStringToObject(doc, "unique_id", DEVID "_playing");
        cJSON_AddStringToObject(doc, "state_topic", TOPIC_PLAYING);
        cJSON_AddStringToObject(doc, "payload_on", "ON");
        cJSON_AddStringToObject(doc, "payload_off", "OFF");
        cJSON_AddStringToObject(doc, "icon", "mdi:piano");
        add_availability(doc);
        cJSON_AddItemToObject(doc, "device", device_block());
        snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/playing/config", PREFIX, DEVID);
        publish_discovery_doc(topic, doc);
    }

    /* Binary sensor: the piano is plugged into the USB port and enumerated. */
    {
        cJSON *doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "name", "USB connection");
        cJSON_AddStringToObject(doc, "unique_id", DEVID "_usb");
        cJSON_AddStringToObject(doc, "state_topic", TOPIC_USB);
        cJSON_AddStringToObject(doc, "payload_on", "ON");
        cJSON_AddStringToObject(doc, "payload_off", "OFF");
        cJSON_AddStringToObject(doc, "device_class", "connectivity");
        cJSON_AddStringToObject(doc, "entity_category", "diagnostic");
        add_availability(doc);
        cJSON_AddItemToObject(doc, "device", device_block());
        snprintf(topic, sizeof(topic), "%s/binary_sensor/%s/usb/config", PREFIX, DEVID);
        publish_discovery_doc(topic, doc);
    }

    /*
     * One device trigger per configured chord. These show up directly in the
     * Home Assistant automation editor as "When <device> ... <chord>".
     */
    for (size_t i = 0; i < s_triggers.trigger_count; i++) {
        const app_trigger_t *t = &s_triggers.triggers[i];

        cJSON *doc = cJSON_CreateObject();
        cJSON_AddStringToObject(doc, "automation_type", "trigger");
        cJSON_AddStringToObject(doc, "platform", "device_automation");
        cJSON_AddStringToObject(doc, "topic", TOPIC_TRIGGER);
        cJSON_AddStringToObject(doc, "type", "chord_played");
        cJSON_AddStringToObject(doc, "subtype", t->name);
        cJSON_AddStringToObject(doc, "payload", t->id);
        cJSON_AddStringToObject(doc, "value_template", "{{ value_json.id }}");
        cJSON_AddItemToObject(doc, "device", device_block());

        snprintf(topic, sizeof(topic), "%s/device_automation/%s/trig_%s/config",
                 PREFIX, DEVID, t->id);
        publish_discovery_doc(topic, doc);
    }

    ESP_LOGI(TAG, "discovery published for %u trigger(s)", (unsigned)s_triggers.trigger_count);
}

/* --------------------------------------------------------------------------
 * Publishing
 * ------------------------------------------------------------------------ */

void app_mqtt_publish_chord(const chord_t *chord)
{
    if (!s_connected || !chord->valid) {
        return;
    }

    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "name", chord->name);
    cJSON_AddStringToObject(doc, "base_name", chord->base_name);
    cJSON_AddBoolToObject(doc, "identified", chord->identified);
    if (chord->identified) {
        cJSON_AddStringToObject(doc, "root", chord_pitch_class_name(chord->root_pc));
        cJSON_AddStringToObject(doc, "quality", chord_quality_suffix(chord->quality));
    }
    cJSON_AddStringToObject(doc, "bass", chord_pitch_class_name(chord->bass_pc));
    cJSON_AddNumberToObject(doc, "velocity", chord->velocity);

    cJSON *notes = cJSON_CreateArray();
    for (uint8_t i = 0; i < chord->note_count; i++) {
        cJSON_AddItemToArray(notes, cJSON_CreateNumber(chord->notes[i]));
    }
    cJSON_AddItemToObject(doc, "notes", notes);

    char *json = cJSON_PrintUnformatted(doc);
    if (json) {
        publish(TOPIC_CHORD, json, 0, 1 /* retained: survives an HA restart */);
        free(json);
    }
    cJSON_Delete(doc);
}

void app_mqtt_publish_trigger(const app_trigger_t *trigger, const chord_t *chord)
{
    cJSON *doc = cJSON_CreateObject();
    cJSON_AddStringToObject(doc, "id", trigger->id);
    cJSON_AddStringToObject(doc, "name", trigger->name);
    cJSON_AddStringToObject(doc, "chord", chord->name);
    cJSON_AddNumberToObject(doc, "velocity", chord->velocity);

    char *json = cJSON_PrintUnformatted(doc);
    if (json) {
        /* Not retained: a trigger is an event, it must not replay on restart. */
        publish(TOPIC_TRIGGER, json, 1, 0);
        free(json);
    }
    cJSON_Delete(doc);
    ESP_LOGI(TAG, "trigger %s (%s)", trigger->id, chord->name);
}

void app_mqtt_publish_playing(bool playing)
{
    publish(TOPIC_PLAYING, playing ? "ON" : "OFF", 0, 1);
}

void app_mqtt_publish_usb(bool connected)
{
    publish(TOPIC_USB, connected ? "ON" : "OFF", 1, 1);
}

void app_mqtt_publish_raw_midi(const usb_midi_msg_t *msg)
{
    char payload[96];
    snprintf(payload, sizeof(payload),
             "{\"status\":%u,\"channel\":%u,\"d1\":%u,\"d2\":%u}",
             msg->status, msg->channel, msg->data1, msg->data2);
    publish(TOPIC_MIDI, payload, 0, 0);
}

/* --------------------------------------------------------------------------
 * Client lifecycle
 * ------------------------------------------------------------------------ */

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected to %s", CONFIG_PIANO_MQTT_URI);
        s_connected = true;
        esp_mqtt_client_publish(s_client, TOPIC_STATUS, "online", 0, 1, 1);
        publish_discovery();
        app_mqtt_publish_usb(usb_midi_host_is_connected());
        app_mqtt_publish_playing(false);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnected");
        s_connected = false;
        break;

    case MQTT_EVENT_ERROR:
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "transport error: %s",
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else {
            ESP_LOGE(TAG, "error type %d, connect return code %d",
                     event->error_handle->error_type,
                     event->error_handle->connect_return_code);
        }
        break;

    default:
        break;
    }
}

esp_err_t app_mqtt_start(const app_triggers_t *triggers)
{
    s_triggers = *triggers;

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_PIANO_MQTT_URI,
        .session.last_will = {
            .topic  = TOPIC_STATUS,
            .msg    = "offline",
            .msg_len = 7,
            .qos    = 1,
            .retain = 1,
        },
        .session.keepalive = 30,
        .network.reconnect_timeout_ms = 5000,
    };
    if (strlen(CONFIG_PIANO_MQTT_USERNAME) > 0) {
        cfg.credentials.username = CONFIG_PIANO_MQTT_USERNAME;
        cfg.credentials.authentication.password = CONFIG_PIANO_MQTT_PASSWORD;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL));
    return esp_mqtt_client_start(s_client);
}
