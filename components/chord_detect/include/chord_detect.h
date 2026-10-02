/*
 * Chord detection from a stream of MIDI note on/off events.
 *
 * This module is deliberately free of any ESP-IDF or FreeRTOS dependency: it
 * takes a caller-supplied millisecond timestamp with every event, so it can be
 * exercised by the host-side unit tests in test/ as well as on the target.
 */
#ifndef CHORD_DETECT_H
#define CHORD_DETECT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CHORD_MAX_NOTES 10
#define CHORD_NAME_LEN  16

typedef enum {
    CHORD_QUALITY_UNKNOWN = 0,
    CHORD_QUALITY_5,
    CHORD_QUALITY_MAJ,
    CHORD_QUALITY_MIN,
    CHORD_QUALITY_DIM,
    CHORD_QUALITY_AUG,
    CHORD_QUALITY_SUS4,
    CHORD_QUALITY_SUS2,
    CHORD_QUALITY_MAJ7,
    CHORD_QUALITY_DOM7,
    CHORD_QUALITY_MIN7,
    CHORD_QUALITY_MIN_MAJ7,
    CHORD_QUALITY_M7B5,
    CHORD_QUALITY_DIM7,
    CHORD_QUALITY_6,
    CHORD_QUALITY_MIN6,
    CHORD_QUALITY_ADD9,
    CHORD_QUALITY_MADD9,
    CHORD_QUALITY_7SUS4,
    CHORD_QUALITY_MAJ9,
    CHORD_QUALITY_DOM9,
    CHORD_QUALITY_MIN9,
    CHORD_QUALITY_69,
    CHORD_QUALITY_7B9,
    CHORD_QUALITY_7SHARP9,
    CHORD_QUALITY_9SUS4,
    CHORD_QUALITY_MIN11,
    CHORD_QUALITY_DOM13,
} chord_quality_t;

typedef struct {
    bool            valid;        /* false => chord released / too few notes   */
    bool            identified;   /* true  => matched a known chord template   */
    uint8_t         root_pc;      /* 0..11, only meaningful if identified      */
    uint8_t         bass_pc;      /* pitch class of the lowest sounding note   */
    chord_quality_t quality;
    uint8_t         note_count;
    uint8_t         notes[CHORD_MAX_NOTES];     /* ascending MIDI note numbers */
    uint8_t         velocity;                   /* peak velocity of the chord  */
    char            name[CHORD_NAME_LEN];       /* e.g. "Cmaj7" or "Cmaj7/E"   */
    char            base_name[CHORD_NAME_LEN];  /* same, without the /bass part */
} chord_t;

typedef struct {
    uint16_t stable_ms;         /* quiet time before a note set counts as a chord */
    uint8_t  min_notes;         /* fewest simultaneous notes to report a chord    */
    bool     include_sustained; /* count notes held by the sustain pedal (CC64)   */
} chord_detect_config_t;

typedef struct {
    chord_detect_config_t cfg;

    uint8_t  velocity[128];     /* 0 => key up                       */
    bool     sustained[128];    /* released key still ringing via CC64 */
    bool     pedal_down;
    uint8_t  held_count;

    uint32_t last_change_ms;
    bool     pending;           /* note set changed, waiting for it to settle */

    bool     last_valid;
    char     last_name[CHORD_NAME_LEN];
} chord_detect_t;

/* Resets all state. cfg may be NULL to use defaults (60 ms / 3 notes / no pedal). */
void chord_detect_init(chord_detect_t *st, const chord_detect_config_t *cfg);

void chord_detect_note_on(chord_detect_t *st, uint8_t note, uint8_t velocity, uint32_t now_ms);
void chord_detect_note_off(chord_detect_t *st, uint8_t note, uint32_t now_ms);
void chord_detect_sustain(chord_detect_t *st, bool down, uint32_t now_ms);
void chord_detect_all_notes_off(chord_detect_t *st, uint32_t now_ms);

/*
 * Call regularly (e.g. every 10 ms). Returns true exactly once per change,
 * writing the new chord to *out. A release is reported as out->valid == false.
 */
bool chord_detect_poll(chord_detect_t *st, uint32_t now_ms, chord_t *out);

/* "C", "C#", ... "B" */
const char *chord_pitch_class_name(uint8_t pc);
/* Suffix used when building chord names: "" for major, "m7" for minor 7th, ... */
const char *chord_quality_suffix(chord_quality_t q);

/*
 * Identifies a chord from a set of MIDI notes without any timing involved.
 * Exposed for the unit tests and for matching note sets from configuration.
 * notes must be ascending. Returns false if nothing could be identified, in
 * which case out is still filled in with notes/bass/velocity.
 */
bool chord_identify(const uint8_t *notes, size_t count, chord_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CHORD_DETECT_H */
