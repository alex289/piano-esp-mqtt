/*
 * Host-side tests for the chord detector. Build and run with:  make -C test
 * No ESP-IDF required; chord_detect.c is plain C99.
 */
#include "chord_detect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fail;
static int g_total;

static void expect_name(const char *what, const uint8_t *notes, size_t n, const char *want)
{
    chord_t c;
    chord_identify(notes, n, &c);
    g_total++;
    if (strcmp(c.name, want) != 0) {
        g_fail++;
        printf("  FAIL %-28s got \"%s\", want \"%s\"\n", what, c.name, want);
    } else {
        printf("  ok   %-28s %s\n", what, c.name);
    }
}

#define CHORD(name, want, ...)                                         \
    do {                                                               \
        const uint8_t _n[] = {__VA_ARGS__};                            \
        expect_name(name, _n, sizeof(_n) / sizeof(_n[0]), want);       \
    } while (0)

static void test_triads(void)
{
    puts("triads");
    CHORD("C major",        "C",      60, 64, 67);
    CHORD("C minor",        "Cm",     60, 63, 67);
    CHORD("C diminished",   "Cdim",   60, 63, 66);
    CHORD("C augmented",    "Caug",   60, 64, 68);
    CHORD("C sus4",         "Csus4",  60, 65, 67);
    CHORD("D sus2",         "Dsus2",  62, 64, 69);
    CHORD("F# major",       "F#",     66, 70, 73);
}

static void test_inversions(void)
{
    puts("inversions (slash names, root != bass)");
    CHORD("C/E  1st inv",   "C/E",    64, 67, 72);
    CHORD("C/G  2nd inv",   "C/G",    67, 72, 76);
    CHORD("Am/C",           "Am/C",   60, 64, 69);
    CHORD("G7/B",           "G7/B",   59, 65, 67, 74);
}

static void test_octave_independence(void)
{
    puts("octave and doubling independence");
    CHORD("C major low",    "C",      36, 40, 43);
    CHORD("C major high",   "C",      84, 88, 91);
    CHORD("C major spread", "C",      48, 64, 79);
    CHORD("C major doubled","C",      48, 60, 64, 67, 72);
}

static void test_sevenths(void)
{
    puts("sevenths and sixths");
    CHORD("Cmaj7",          "Cmaj7",  60, 64, 67, 71);
    CHORD("C7",             "C7",     60, 64, 67, 70);
    CHORD("Cm7",            "Cm7",    60, 63, 67, 70);
    CHORD("CmMaj7",         "CmMaj7", 60, 63, 67, 71);
    CHORD("Cm7b5",          "Cm7b5",  60, 63, 66, 70);
    CHORD("Cdim7",          "Cdim7",  60, 63, 66, 69);
    CHORD("C7sus4",         "C7sus4", 60, 65, 67, 70);
}

static void test_bass_disambiguation(void)
{
    /* These pairs are the *same* pitch-class set; only the bass tells them apart. */
    puts("bass disambiguation");
    CHORD("C,E,G,A over C  -> C6",   "C6",    60, 64, 67, 69);
    CHORD("A,C,E,G over A  -> Am7",  "Am7",   57, 60, 64, 67);
    CHORD("C,Eb,G,A over C -> Cm6",  "Cm6",   60, 63, 67, 69);
    CHORD("A,C,Eb,G over A -> Am7b5","Am7b5", 57, 60, 63, 67);
    CHORD("C,D,G over C    -> Csus2", "Csus2", 60, 62, 67);
    CHORD("G,C,D over G    -> Gsus4", "Gsus4", 55, 60, 62);
}

static void test_extensions(void)
{
    puts("extensions");
    CHORD("Cadd9",          "Cadd9",  60, 62, 64, 67);
    CHORD("Cmaj9",          "Cmaj9",  60, 62, 64, 67, 71);
    CHORD("C9",             "C9",     60, 62, 64, 67, 70);
    CHORD("Cm9",            "Cm9",    60, 62, 63, 67, 70);
    CHORD("C6/9",           "C6/9",   60, 62, 64, 67, 69);
    CHORD("C7b9",           "C7b9",   60, 61, 64, 67, 70);
    CHORD("C7#9 (Hendrix)", "C7#9",   60, 63, 64, 67, 70);
}

static void check(const char *what, bool cond)
{
    g_total++;
    if (!cond) {
        g_fail++;
        printf("  FAIL %s\n", what);
    } else {
        printf("  ok   %s\n", what);
    }
}

static void test_timing(void)
{
    puts("timing / debounce");
    chord_detect_t st;
    chord_detect_config_t cfg = { .stable_ms = 50, .min_notes = 3 };
    chord_detect_init(&st, &cfg);
    chord_t c;

    /* A rolled chord: notes arrive 10 ms apart, so only the final set fires. */
    chord_detect_note_on(&st, 60, 80, 0);
    check("no chord after 1 note", !chord_detect_poll(&st, 60, &c));
    chord_detect_note_on(&st, 64, 82, 10);
    check("no chord after 2 notes", !chord_detect_poll(&st, 60, &c));
    chord_detect_note_on(&st, 67, 90, 20);
    check("not yet stable at t=60", !chord_detect_poll(&st, 60, &c));
    check("fires once stable at t=75", chord_detect_poll(&st, 75, &c));
    check("  -> C major", strcmp(c.name, "C") == 0);
    check("  -> peak velocity 90", c.velocity == 90);
    check("no repeat event", !chord_detect_poll(&st, 200, &c));

    /* Adding a doubled octave must not re-fire: same chord name. */
    chord_detect_note_on(&st, 72, 70, 300);
    check("octave double does not re-fire", !chord_detect_poll(&st, 400, &c));

    /* Changing to a different chord fires again. */
    chord_detect_note_off(&st, 64, 500);
    chord_detect_note_on(&st, 63, 85, 505);
    check("chord change fires", chord_detect_poll(&st, 600, &c));
    check("  -> Cm", strcmp(c.name, "Cm") == 0);

    /* Lifting the hands reports a release exactly once. */
    chord_detect_all_notes_off(&st, 700);
    check("release fires", chord_detect_poll(&st, 800, &c));
    check("  -> invalid", !c.valid);
    check("release fires only once", !chord_detect_poll(&st, 900, &c));
}

static void test_sustain(void)
{
    puts("sustain pedal");
    chord_detect_t st;
    chord_detect_config_t cfg = { .stable_ms = 50, .min_notes = 3, .include_sustained = true };
    chord_detect_init(&st, &cfg);
    chord_t c;

    chord_detect_sustain(&st, true, 0);
    chord_detect_note_on(&st, 60, 80, 0);
    chord_detect_note_on(&st, 64, 80, 5);
    chord_detect_note_on(&st, 67, 80, 10);
    check("chord detected", chord_detect_poll(&st, 100, &c));
    check("  -> C", strcmp(c.name, "C") == 0);

    /* Keys released while the pedal is down: the chord keeps ringing. */
    chord_detect_note_off(&st, 60, 120);
    chord_detect_note_off(&st, 64, 125);
    chord_detect_note_off(&st, 67, 130);
    check("no release while pedal down", !chord_detect_poll(&st, 300, &c));

    chord_detect_sustain(&st, false, 400);
    check("release on pedal up", chord_detect_poll(&st, 500, &c));
    check("  -> invalid", !c.valid);
}

static void test_note_on_velocity_zero(void)
{
    puts("note-on with velocity 0 is a note-off");
    chord_detect_t st;
    chord_detect_config_t cfg = { .stable_ms = 10, .min_notes = 3 };
    chord_detect_init(&st, &cfg);
    chord_t c;

    chord_detect_note_on(&st, 60, 80, 0);
    chord_detect_note_on(&st, 64, 80, 0);
    chord_detect_note_on(&st, 67, 80, 0);
    check("chord detected", chord_detect_poll(&st, 50, &c));
    chord_detect_note_on(&st, 64, 0, 60); /* running-status note off */
    check("release fires", chord_detect_poll(&st, 100, &c));
    check("  -> invalid", !c.valid);
}

static void test_unidentified(void)
{
    puts("unidentified cluster");
    chord_t c;
    const uint8_t cluster[] = { 60, 61, 62 };
    chord_identify(cluster, 3, &c);
    check("cluster is not identified", !c.identified);
    check("cluster still reports notes", c.note_count == 3 && c.notes[0] == 60);
}

int main(void)
{
    test_triads();
    test_inversions();
    test_octave_independence();
    test_sevenths();
    test_bass_disambiguation();
    test_extensions();
    test_timing();
    test_sustain();
    test_note_on_velocity_zero();
    test_unidentified();

    printf("\n%d/%d checks passed\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
