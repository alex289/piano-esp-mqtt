#ifndef APP_TRIGGERS_H
#define APP_TRIGGERS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "chord_detect.h"
#include "esp_err.h"

#define APP_MAX_TRIGGERS 24
#define APP_TRIGGER_ID_LEN 24
#define APP_TRIGGER_NAME_LEN 40

typedef struct {
    char     id[APP_TRIGGER_ID_LEN];
    char     name[APP_TRIGGER_NAME_LEN];
    char     chord[CHORD_NAME_LEN];   /* empty when matching on exact notes */
    bool     require_bass;            /* set when "chord" contained a /bass  */
    uint8_t  notes[CHORD_MAX_NOTES];
    uint8_t  note_count;
    uint32_t last_fired_ms;
} app_trigger_t;

typedef struct {
    chord_detect_config_t detect;
    uint32_t              retrigger_ms;
    app_trigger_t         triggers[APP_MAX_TRIGGERS];
    size_t                trigger_count;
} app_triggers_t;

/* Parses the triggers.json embedded in the firmware image. */
esp_err_t app_triggers_load(app_triggers_t *cfg);

/*
 * Returns the trigger that the chord fires, or NULL. Honours retrigger_ms,
 * so calling this for every detected chord is safe.
 */
app_trigger_t *app_triggers_match(app_triggers_t *cfg, const chord_t *chord, uint32_t now_ms);

#endif /* APP_TRIGGERS_H */
