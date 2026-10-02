#include "chord_detect.h"

#include <string.h>
#include <stdio.h>

#define PC_MASK 0x0FFFu

static const char *const k_pc_names[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

/*
 * Interval templates, as a bitmask of semitones above the root.
 * Matching is an exact pitch-class-set comparison, so templates of different
 * sizes can never collide; the order below only breaks ties between templates
 * of equal size (e.g. C6 vs Am7, which are the same four pitch classes).
 */
typedef struct {
    chord_quality_t q;
    uint16_t        mask;
    const char     *suffix;
} chord_template_t;

#define IV(...) (iv_mask((const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__})))

static uint16_t iv_mask(const uint8_t *iv, size_t n)
{
    uint16_t m = 0;
    for (size_t i = 0; i < n; i++) {
        m |= (uint16_t)(1u << iv[i]);
    }
    return m;
}

/* Built once at first use so the table can stay readable. */
static chord_template_t s_templates[] = {
    /* triads */
    { CHORD_QUALITY_MAJ,      0, ""      },
    { CHORD_QUALITY_MIN,      0, "m"     },
    { CHORD_QUALITY_DIM,      0, "dim"   },
    { CHORD_QUALITY_AUG,      0, "aug"   },
    { CHORD_QUALITY_SUS4,     0, "sus4"  },
    { CHORD_QUALITY_SUS2,     0, "sus2"  },
    /* sevenths and sixths */
    { CHORD_QUALITY_MAJ7,     0, "maj7"  },
    { CHORD_QUALITY_DOM7,     0, "7"     },
    { CHORD_QUALITY_MIN7,     0, "m7"    },
    { CHORD_QUALITY_MIN_MAJ7, 0, "mMaj7" },
    { CHORD_QUALITY_M7B5,     0, "m7b5"  },
    { CHORD_QUALITY_DIM7,     0, "dim7"  },
    { CHORD_QUALITY_6,        0, "6"     },
    { CHORD_QUALITY_MIN6,     0, "m6"    },
    { CHORD_QUALITY_ADD9,     0, "add9"  },
    { CHORD_QUALITY_MADD9,    0, "madd9" },
    { CHORD_QUALITY_7SUS4,    0, "7sus4" },
    /* extensions */
    { CHORD_QUALITY_MAJ9,     0, "maj9"  },
    { CHORD_QUALITY_DOM9,     0, "9"     },
    { CHORD_QUALITY_MIN9,     0, "m9"    },
    { CHORD_QUALITY_69,       0, "6/9"   },
    { CHORD_QUALITY_7B9,      0, "7b9"   },
    { CHORD_QUALITY_7SHARP9,  0, "7#9"   },
    { CHORD_QUALITY_9SUS4,    0, "9sus4" },
    { CHORD_QUALITY_MIN11,    0, "m11"   },
    { CHORD_QUALITY_DOM13,    0, "13"    },
    /* dyad, last so it never shadows a triad */
    { CHORD_QUALITY_5,        0, "5"     },
};

#define N_TEMPLATES (sizeof(s_templates) / sizeof(s_templates[0]))

static bool s_tables_ready;

static void build_tables(void)
{
    if (s_tables_ready) {
        return;
    }
    size_t i = 0;
    s_templates[i++].mask = IV(0, 4, 7);            /* maj      */
    s_templates[i++].mask = IV(0, 3, 7);            /* min      */
    s_templates[i++].mask = IV(0, 3, 6);            /* dim      */
    s_templates[i++].mask = IV(0, 4, 8);            /* aug      */
    s_templates[i++].mask = IV(0, 5, 7);            /* sus4     */
    s_templates[i++].mask = IV(0, 2, 7);            /* sus2     */
    s_templates[i++].mask = IV(0, 4, 7, 11);        /* maj7     */
    s_templates[i++].mask = IV(0, 4, 7, 10);        /* 7        */
    s_templates[i++].mask = IV(0, 3, 7, 10);        /* m7       */
    s_templates[i++].mask = IV(0, 3, 7, 11);        /* mMaj7    */
    s_templates[i++].mask = IV(0, 3, 6, 10);        /* m7b5     */
    s_templates[i++].mask = IV(0, 3, 6, 9);         /* dim7     */
    s_templates[i++].mask = IV(0, 4, 7, 9);         /* 6        */
    s_templates[i++].mask = IV(0, 3, 7, 9);         /* m6       */
    s_templates[i++].mask = IV(0, 2, 4, 7);         /* add9     */
    s_templates[i++].mask = IV(0, 2, 3, 7);         /* madd9    */
    s_templates[i++].mask = IV(0, 5, 7, 10);        /* 7sus4    */
    s_templates[i++].mask = IV(0, 2, 4, 7, 11);     /* maj9     */
    s_templates[i++].mask = IV(0, 2, 4, 7, 10);     /* 9        */
    s_templates[i++].mask = IV(0, 2, 3, 7, 10);     /* m9       */
    s_templates[i++].mask = IV(0, 2, 4, 7, 9);      /* 6/9      */
    s_templates[i++].mask = IV(0, 1, 4, 7, 10);     /* 7b9      */
    s_templates[i++].mask = IV(0, 3, 4, 7, 10);     /* 7#9      */
    s_templates[i++].mask = IV(0, 2, 5, 7, 10);     /* 9sus4    */
    s_templates[i++].mask = IV(0, 2, 3, 5, 7, 10);  /* m11      */
    s_templates[i++].mask = IV(0, 2, 4, 7, 9, 10);  /* 13       */
    s_templates[i++].mask = IV(0, 7);               /* 5        */

    s_tables_ready = true;
}

const char *chord_pitch_class_name(uint8_t pc)
{
    return k_pc_names[pc % 12];
}

const char *chord_quality_suffix(chord_quality_t q)
{
    build_tables();
    for (size_t i = 0; i < N_TEMPLATES; i++) {
        if (s_templates[i].q == q) {
            return s_templates[i].suffix;
        }
    }
    return "";
}

static uint16_t rotate_up(uint16_t mask, uint8_t semitones)
{
    return (uint16_t)(((mask << semitones) | (mask >> (12 - semitones))) & PC_MASK);
}

bool chord_identify(const uint8_t *notes, size_t count, chord_t *out)
{
    build_tables();
    memset(out, 0, sizeof(*out));

    if (count == 0 || count > CHORD_MAX_NOTES) {
        return false;
    }

    uint16_t pcset = 0;
    for (size_t i = 0; i < count; i++) {
        pcset |= (uint16_t)(1u << (notes[i] % 12));
        out->notes[i] = notes[i];
    }
    out->note_count = (uint8_t)count;
    out->bass_pc = notes[0] % 12;
    out->valid = true;

    const int n = __builtin_popcount(pcset);

    /*
     * Pass 1 anchors the root to the bass note. This is what disambiguates the
     * pairs that share a pitch-class set: C,E,G,A over a C bass is C6, over an
     * A bass it is Am7.
     */
    for (size_t t = 0; t < N_TEMPLATES; t++) {
        if (__builtin_popcount(s_templates[t].mask) != n) {
            continue;
        }
        if (rotate_up(s_templates[t].mask, out->bass_pc) == pcset) {
            out->root_pc = out->bass_pc;
            out->quality = s_templates[t].q;
            out->identified = true;
            goto named;
        }
    }

    /* Pass 2: inversions, where the bass is not the root. */
    for (size_t t = 0; t < N_TEMPLATES; t++) {
        if (__builtin_popcount(s_templates[t].mask) != n) {
            continue;
        }
        for (uint8_t root = 0; root < 12; root++) {
            if (root == out->bass_pc) {
                continue; /* already ruled out in pass 1 */
            }
            if (rotate_up(s_templates[t].mask, root) == pcset) {
                out->root_pc = root;
                out->quality = s_templates[t].q;
                out->identified = true;
                goto named;
            }
        }
    }

    return false;

named:
    snprintf(out->base_name, sizeof(out->base_name), "%s%s",
             chord_pitch_class_name(out->root_pc), chord_quality_suffix(out->quality));
    if (out->root_pc == out->bass_pc) {
        memcpy(out->name, out->base_name, sizeof(out->name));
    } else {
        snprintf(out->name, sizeof(out->name), "%s/%s",
                 out->base_name, chord_pitch_class_name(out->bass_pc));
    }
    return true;
}

void chord_detect_init(chord_detect_t *st, const chord_detect_config_t *cfg)
{
    memset(st, 0, sizeof(*st));
    if (cfg) {
        st->cfg = *cfg;
    }
    if (st->cfg.stable_ms == 0) {
        st->cfg.stable_ms = 60;
    }
    if (st->cfg.min_notes == 0) {
        st->cfg.min_notes = 3;
    }
}

static void mark_changed(chord_detect_t *st, uint32_t now_ms)
{
    st->last_change_ms = now_ms;
    st->pending = true;
}

void chord_detect_note_on(chord_detect_t *st, uint8_t note, uint8_t velocity, uint32_t now_ms)
{
    if (note > 127) {
        return;
    }
    /* Running-status note-on with velocity 0 is a note-off. */
    if (velocity == 0) {
        chord_detect_note_off(st, note, now_ms);
        return;
    }
    if (st->velocity[note] == 0) {
        st->held_count++;
    }
    st->velocity[note] = velocity;
    st->sustained[note] = false;
    mark_changed(st, now_ms);
}

void chord_detect_note_off(chord_detect_t *st, uint8_t note, uint32_t now_ms)
{
    if (note > 127 || st->velocity[note] == 0) {
        return;
    }
    if (st->cfg.include_sustained && st->pedal_down) {
        if (!st->sustained[note]) {
            st->sustained[note] = true; /* keeps ringing, set is unchanged */
        }
        return;
    }
    st->velocity[note] = 0;
    st->sustained[note] = false;
    st->held_count--;
    mark_changed(st, now_ms);
}

void chord_detect_sustain(chord_detect_t *st, bool down, uint32_t now_ms)
{
    st->pedal_down = down;
    if (down) {
        return;
    }
    bool changed = false;
    for (int n = 0; n < 128; n++) {
        if (st->sustained[n]) {
            st->sustained[n] = false;
            st->velocity[n] = 0;
            st->held_count--;
            changed = true;
        }
    }
    if (changed) {
        mark_changed(st, now_ms);
    }
}

void chord_detect_all_notes_off(chord_detect_t *st, uint32_t now_ms)
{
    if (st->held_count == 0) {
        return;
    }
    memset(st->velocity, 0, sizeof(st->velocity));
    memset(st->sustained, 0, sizeof(st->sustained));
    st->held_count = 0;
    mark_changed(st, now_ms);
}

bool chord_detect_poll(chord_detect_t *st, uint32_t now_ms, chord_t *out)
{
    if (!st->pending) {
        return false;
    }
    if ((uint32_t)(now_ms - st->last_change_ms) < st->cfg.stable_ms) {
        return false;
    }
    st->pending = false;

    uint8_t notes[CHORD_MAX_NOTES];
    uint8_t count = 0;
    uint8_t peak = 0;
    for (int n = 0; n < 128 && count < CHORD_MAX_NOTES; n++) {
        if (st->velocity[n] != 0) {
            notes[count++] = (uint8_t)n;
            if (st->velocity[n] > peak) {
                peak = st->velocity[n];
            }
        }
    }

    /* Too few notes: report a release, but only once. */
    if (count < st->cfg.min_notes) {
        if (!st->last_valid) {
            return false;
        }
        st->last_valid = false;
        st->last_name[0] = '\0';
        memset(out, 0, sizeof(*out));
        out->valid = false;
        return true;
    }

    chord_identify(notes, count, out);
    out->valid = true;
    out->velocity = peak;

    /* Unidentified sets still get a name so they are visible in Home Assistant. */
    if (!out->identified) {
        snprintf(out->base_name, sizeof(out->base_name), "?%s", chord_pitch_class_name(out->bass_pc));
        memcpy(out->name, out->base_name, sizeof(out->name));
    }

    if (st->last_valid && strcmp(st->last_name, out->name) == 0) {
        return false; /* same chord, e.g. a doubled octave was added */
    }

    st->last_valid = true;
    snprintf(st->last_name, sizeof(st->last_name), "%s", out->name);
    return true;
}
