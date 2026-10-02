#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdbool.h>

#include "app_triggers.h"
#include "chord_detect.h"
#include "esp_err.h"
#include "usb_midi_host.h"

/*
 * Starts the MQTT client. The trigger config is kept so that Home Assistant
 * discovery can be re-published on every reconnect.
 */
esp_err_t app_mqtt_start(const app_triggers_t *triggers);

bool app_mqtt_is_connected(void);

void app_mqtt_publish_chord(const chord_t *chord);
void app_mqtt_publish_trigger(const app_trigger_t *trigger, const chord_t *chord);
void app_mqtt_publish_playing(bool playing);
void app_mqtt_publish_usb(bool connected);
void app_mqtt_publish_raw_midi(const usb_midi_msg_t *msg);

#endif /* APP_MQTT_H */
