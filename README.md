# piano-esp-mqtt

Reads MIDI from a **Yamaha P-115** over USB with an **ESP32-S3**, recognises
the chord being played, and publishes it to **Home Assistant** over MQTT.
Configured chords fire Home Assistant device triggers, so you can play a
`Cmaj7` to turn on the lights.

```
Yamaha P-115 ──USB──> ESP32-S3 (USB host) ──WiFi/MQTT──> Home Assistant
             MIDI            chord detection        discovery + triggers
```

## How it works

The P-115 is a class-compliant USB MIDI device, so no vendor driver is needed —
but it is a *device*, which means the ESP32-S3 has to be the USB **host**.
`components/usb_midi_host` is a small class driver on top of the ESP-IDF USB
Host Library: it finds the MIDIStreaming interface (class `0x01`, subclass
`0x03`), claims it, and decodes the 4-byte USB-MIDI event packets from its bulk
IN endpoint.

`components/chord_detect` turns the note stream into chords. It waits for the
set of held keys to stay unchanged for a moment, reduces it to pitch classes
and matches it against 27 interval templates, so it is independent of octave,
voicing and doubling. The bass note decides between chords that share the same
pitch classes — `C E G A` is `C6` over a C and `Am7` over an A.

## Layout

```
components/usb_midi_host/   USB MIDI 1.0 host class driver
components/chord_detect/    note stream -> chord name (no ESP dependencies)
main/                       WiFi, MQTT, Home Assistant discovery, glue
main/triggers.json          chord -> trigger mapping, embedded in the image
test/                       host-side tests for the chord detector
docs/HARDWARE.md            wiring, and why the native USB-C socket is not enough
docs/HOME_ASSISTANT.md      entities, triggers, topics
```

## Wiring

**Short version:** a USB-A socket breakout wired to `5V`, `19`, `20`, `G`, then
a normal USB-A/USB-B cable to the piano. The native USB-C socket cannot be used
directly because the DevKitC-1 does not put 5 V on its VBUS pin, and the piano
will not talk without it. Full explanation and the diagram:
**[docs/HARDWARE.md](docs/HARDWARE.md)**.

## Build

Needs ESP-IDF **v5.3 or newer**.

```bash
cp credentials.defaults.example credentials.defaults   # then fill it in

. $IDF_PATH/export.sh
idf.py build flash monitor
```

`credentials.defaults` is gitignored and appended to `SDKCONFIG_DEFAULTS`
automatically, so secrets never reach the repository and no `menuconfig`
round-trip is needed. Every other setting is still under
`idf.py menuconfig` -> *Piano MQTT Bridge*.

Without credentials the firmware still boots and runs: USB MIDI and chord
detection work, and recognised chords are written to the log instead of being
published. That is the quickest way to bring up the hardware.

### Building without a local ESP-IDF

```bash
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.5 idf.py build
```

Flashing has to happen on the host, since Docker Desktop on macOS cannot pass
through USB serial ports:

```bash
cd build && esptool --chip esp32s3 --port /dev/cu.usbmodem<...> -b 460800 \
    --before default-reset --after hard-reset write-flash "@flash_args"
```

## Configuring chords

Edit `main/triggers.json` and re-flash. It is embedded in the firmware image
and parsed at boot.

```json
{
  "stable_ms": 60,
  "min_notes": 3,
  "include_sustained": false,
  "retrigger_ms": 1500,

  "triggers": [
    { "id": "licht_an",  "name": "Licht an",  "chord": "Cmaj7" },
    { "id": "licht_aus", "name": "Licht aus", "chord": "Fm" },
    { "id": "gruss",     "name": "Gruss",     "notes": [60, 64, 67, 72] }
  ]
}
```

| Key | Meaning |
|---|---|
| `stable_ms` | How long the held keys must stay unchanged before they count as a chord. Raise it if rolled chords fire intermediate matches. |
| `min_notes` | Fewest keys held at once to consider a chord at all. |
| `include_sustained` | Count notes still ringing via the sustain pedal (CC64). |
| `retrigger_ms` | A trigger will not fire again within this window. |
| `chord` | Matches any voicing, inversion and octave, e.g. `Cmaj7`. Write `C/E` to additionally require E in the bass. |
| `notes` | Exact MIDI note numbers — octave-specific, nothing else may sound. |

Recognised qualities: major, `m`, `dim`, `aug`, `sus2`, `sus4`, `maj7`, `7`,
`m7`, `mMaj7`, `m7b5`, `dim7`, `6`, `m6`, `add9`, `madd9`, `7sus4`, `maj9`,
`9`, `m9`, `6/9`, `7b9`, `7#9`, `9sus4`, `m11`, `13`, `5`.

Not sure what a chord is called? Subscribe to the topic and play it:

```bash
mosquitto_sub -h homeassistant.local -v -t 'piano/#'
```

## Home Assistant

Entities appear automatically via MQTT discovery; each configured chord becomes
a device trigger in the automation editor. See
**[docs/HOME_ASSISTANT.md](docs/HOME_ASSISTANT.md)**.

## Tests

The chord detector has no ESP-IDF dependencies and is tested on the host:

```bash
make -C test check
```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `waiting for a MIDI device` forever, nothing on connect | No VBUS on the USB-A breakout. Check the 5 V jumper, and that the board itself is powered through the `UART` socket. |
| Device attaches, then `no USB MIDI interface found` | Something other than the piano is plugged in, or D+/D− are swapped. |
| Enumerates, then disconnects or stalls | D+/D− wires too long or not routed as a pair. Shorten them. |
| Chords are recognised but Home Assistant shows nothing | MQTT credentials; check `idf.py monitor` for the `mqtt` tag. |
| Rolled chords fire the wrong trigger | Raise `stable_ms` in `triggers.json`. |
| A trigger fires repeatedly | Raise `retrigger_ms`. |
