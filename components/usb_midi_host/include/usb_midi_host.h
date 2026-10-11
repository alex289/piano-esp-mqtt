/*
 * Minimal USB MIDI 1.0 class host driver on top of the ESP-IDF USB Host Library.
 *
 * Claims the first interface with bInterfaceClass == 0x01 (Audio) and
 * bInterfaceSubClass == 0x03 (MIDIStreaming), then streams 4-byte USB-MIDI
 * event packets from its bulk IN endpoint and decodes the channel voice
 * messages. That is all a class-compliant keyboard such as the Yamaha P-115
 * needs; no vendor driver is involved.
 */
#ifndef USB_MIDI_HOST_H
#define USB_MIDI_HOST_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Status nibbles of the MIDI channel voice messages we decode. */
#define MIDI_NOTE_OFF         0x80
#define MIDI_NOTE_ON          0x90
#define MIDI_POLY_PRESSURE    0xA0
#define MIDI_CONTROL_CHANGE   0xB0
#define MIDI_PROGRAM_CHANGE   0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND       0xE0

#define MIDI_CC_SUSTAIN       64
#define MIDI_CC_ALL_NOTES_OFF 123

typedef struct {
    uint8_t cable;   /* USB-MIDI virtual cable number, 0 for the P-115 */
    uint8_t status;  /* status nibble only, e.g. 0x90 */
    uint8_t channel; /* 0..15 */
    uint8_t data1;
    uint8_t data2;
} usb_midi_msg_t;

typedef struct {
    uint16_t vid;
    uint16_t pid;
} usb_midi_dev_info_t;

/* Both callbacks run on the driver's own task. Keep them short and non-blocking. */
typedef void (*usb_midi_msg_cb_t)(const usb_midi_msg_t *msg, void *arg);
typedef void (*usb_midi_conn_cb_t)(bool connected, const usb_midi_dev_info_t *info, void *arg);

typedef struct {
    usb_midi_msg_cb_t  on_message;
    usb_midi_conn_cb_t on_connection;
    void              *arg;
    int                task_priority; /* 0 => 5  */
    int                task_stack;    /* 0 => 4096 */
    bool               probe_lines;   /* log the bare D+/D- levels once at boot */
} usb_midi_host_config_t;

/* Installs the USB Host Library and starts the daemon and client tasks. */
esp_err_t usb_midi_host_start(const usb_midi_host_config_t *config);

bool usb_midi_host_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* USB_MIDI_HOST_H */
