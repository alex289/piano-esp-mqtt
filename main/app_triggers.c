#include "app_triggers.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "triggers";

extern const char triggers_json_start[] asm("_binary_triggers_json_start");
extern const char triggers_json_end[]   asm("_binary_triggers_json_end");

static void copy_str(char *dst, size_t dst_len, const cJSON *item)
{
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, dst_len, "%s", item->valuestring);
    }
}

static int get_int(const cJSON *root, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static bool get_bool(const cJSON *root, const char *key, bool fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsBool(item) ? cJSON_IsTrue(item) : fallback;
}

esp_err_t app_triggers_load(app_triggers_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    const size_t len = (size_t)(triggers_json_end - triggers_json_start);
    cJSON *root = cJSON_ParseWithLength(triggers_json_start, len);
    if (root == NULL) {
        ESP_LOGE(TAG, "triggers.json is not valid JSON (near: %s)",
                 cJSON_GetErrorPtr() ? cJSON_GetErrorPtr() : "?");
        return ESP_ERR_INVALID_ARG;
    }

    cfg->detect.stable_ms = (uint16_t)get_int(root, "stable_ms", 60);
    cfg->detect.min_notes = (uint8_t)get_int(root, "min_notes", 3);
    cfg->detect.include_sustained = get_bool(root, "include_sustained", false);
    cfg->retrigger_ms = (uint32_t)get_int(root, "retrigger_ms", 1500);

    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "triggers");
    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, list) {
        if (cfg->trigger_count >= APP_MAX_TRIGGERS) {
            ESP_LOGW(TAG, "more than %d triggers, ignoring the rest", APP_MAX_TRIGGERS);
            break;
        }
        app_trigger_t *t = &cfg->triggers[cfg->trigger_count];
        memset(t, 0, sizeof(*t));

        copy_str(t->id, sizeof(t->id), cJSON_GetObjectItemCaseSensitive(entry, "id"));
        copy_str(t->name, sizeof(t->name), cJSON_GetObjectItemCaseSensitive(entry, "name"));
        copy_str(t->chord, sizeof(t->chord), cJSON_GetObjectItemCaseSensitive(entry, "chord"));

        if (t->id[0] == '\0') {
            ESP_LOGW(TAG, "trigger without an \"id\", skipped");
            continue;
        }
        if (t->name[0] == '\0') {
            /* Not snprintf: source and destination are both inside *cfg. */
            memcpy(t->name, t->id, strlen(t->id) + 1);
        }
        t->require_bass = (strchr(t->chord, '/') != NULL);

        const cJSON *notes = cJSON_GetObjectItemCaseSensitive(entry, "notes");
        const cJSON *note = NULL;
        cJSON_ArrayForEach(note, notes) {
            if (t->note_count >= CHORD_MAX_NOTES) {
                break;
            }
            if (cJSON_IsNumber(note) && note->valueint >= 0 && note->valueint <= 127) {
                t->notes[t->note_count++] = (uint8_t)note->valueint;
            }
        }

        if (t->chord[0] == '\0' && t->note_count == 0) {
            ESP_LOGW(TAG, "trigger \"%s\" has neither \"chord\" nor \"notes\", skipped", t->id);
            continue;
        }

        ESP_LOGI(TAG, "trigger %-14s -> %s", t->id,
                 t->chord[0] ? t->chord : "<note set>");
        cfg->trigger_count++;
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "%u trigger(s) loaded, stable_ms=%u min_notes=%u retrigger_ms=%u",
             (unsigned)cfg->trigger_count, cfg->detect.stable_ms,
             cfg->detect.min_notes, (unsigned)cfg->retrigger_ms);
    return ESP_OK;
}

static bool notes_equal(const app_trigger_t *t, const chord_t *chord)
{
    if (t->note_count != chord->note_count) {
        return false;
    }
    /* Both lists are ascending. */
    return memcmp(t->notes, chord->notes, t->note_count) == 0;
}

app_trigger_t *app_triggers_match(app_triggers_t *cfg, const chord_t *chord, uint32_t now_ms)
{
    if (!chord->valid) {
        return NULL;
    }

    for (size_t i = 0; i < cfg->trigger_count; i++) {
        app_trigger_t *t = &cfg->triggers[i];
        bool hit = false;

        if (t->note_count > 0) {
            hit = notes_equal(t, chord);
        } else if (chord->identified) {
            /*
             * Without an explicit bass a trigger matches any inversion, so
             * "Cmaj7" also fires for Cmaj7/E. With a slash it must match exactly.
             */
            const char *played = t->require_bass ? chord->name : chord->base_name;
            hit = (strcasecmp(t->chord, played) == 0);
        }

        if (!hit) {
            continue;
        }
        if (t->last_fired_ms != 0 &&
            (uint32_t)(now_ms - t->last_fired_ms) < cfg->retrigger_ms) {
            return NULL; /* still inside the retrigger guard */
        }
        t->last_fired_ms = now_ms ? now_ms : 1;
        return t;
    }
    return NULL;
}
