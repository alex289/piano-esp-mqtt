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
tools/monitor.py            serial log reader (idf.py monitor needs local IDF)
Makefile                    wrappers for build / flash / monitor
docs/HARDWARE.md            wiring, and why the native USB-C socket is not enough
docs/HOME_ASSISTANT.md      entities, triggers, topics
```

## Wiring

**Short version:** a USB-A socket breakout wired to `5V`, `19`, `20`, `G`, then
a normal USB-A/USB-B cable to the piano. The native USB-C socket cannot be used
directly because the DevKitC-1 does not put 5 V on its VBUS pin, and the piano
will not talk without it. Full explanation and the diagram:
**[docs/HARDWARE.md](docs/HARDWARE.md)**.

## Setup

### One-off: the toolchain

Nothing has to be installed system-wide. ESP-IDF runs in a container, and the
only host-side tool is `esptool` (for flashing) plus `pyserial` (for the log),
which live in their own virtualenv:

```bash
python3 -m venv ~/.esptool-venv
~/.esptool-venv/bin/pip install esptool
docker pull espressif/idf:v5.5
```

### One-off: credentials

```bash
cp credentials.defaults.example credentials.defaults   # then fill it in
```

`credentials.defaults` is gitignored and appended to `SDKCONFIG_DEFAULTS` by
the top-level `CMakeLists.txt`, so secrets never reach the repository and no
`menuconfig` round-trip is needed.

ESP-IDF applies `SDKCONFIG_DEFAULTS` only when `sdkconfig` does not exist yet,
so editing this file after a first build would otherwise be ignored without a
word — and you would flash a firmware with no WiFi configured. `make build`
notices and regenerates `sdkconfig`; if you build by hand, delete `sdkconfig`
yourself (or run `make reconfigure`) after changing it.

Leave username and password empty for a broker that allows anonymous access
(Mosquitto started with `mosquitto-no-auth.conf`):

```
CONFIG_PIANO_WIFI_SSID="MeinWLAN"
CONFIG_PIANO_WIFI_PASSWORD="geheim"
CONFIG_PIANO_MQTT_URI="mqtt://192.168.1.50:1883"
CONFIG_PIANO_MQTT_USERNAME=""
CONFIG_PIANO_MQTT_PASSWORD=""
```

Without credentials the firmware still boots: USB MIDI and chord detection work
and recognised chords go to the log instead of MQTT. That is the quickest way
to bring up the hardware.

### Which cable in which socket

The board has two USB-C sockets and they are *not* interchangeable. Use the one
marked **UART** for flashing, power and the log; the one marked **USB** belongs
to the piano. `make port` tells you what is connected:

```
$ make port
/dev/cu.usbmodem5C941343941
  |       "USB Product Name" = "USB Single Serial"
```

* `USB Single Serial` / `1a86:55d3` — the CH343 bridge, i.e. the **UART**
  socket. This is the one you want.
* `Espressif Systems` / `303a:xxxx` — the **native** socket. The firmware takes
  that controller over for USB host mode, so the port disappears as soon as the
  app starts and you get no log at all.

## Commands

All of these are wrappers around longer commands; run `make help` for the list.

| Command | What it does |
|---|---|
| `make build` | Compiles in the ESP-IDF container. |
| `make flash` | Writes the firmware to the board. |
| `make monitor` | **Reads the log.** Resets the board first so the boot output is not missed. Ctrl-C quits. |
| `make run` | `build` + `flash` + `monitor`, the usual edit/test loop. |
| `make test` | Runs the chord detector tests on your Mac, no hardware needed. |
| `make menuconfig` | ESP-IDF configuration menu (interactive). |
| `make size` | Where flash and RAM are going. |
| `make erase` | Erases the entire flash. Use when the board behaves strangely. |
| `make clean` / `make fullclean` | Deletes build output. |
| `make port` | Shows the detected serial port and what is on it. |
| `make reconfigure` | Drops `sdkconfig` so `credentials.defaults` is applied again. `make build` does this automatically. |

The port is detected automatically; override it if you have several boards
plugged in:

```bash
make flash PORT=/dev/cu.usbmodem5C941343941
```

### Reading the log

```bash
make monitor
```

This is the one you will use most. It resets the board and streams the serial
output, so you see the whole boot sequence:

```
I (353) triggers: 5 trigger(s) loaded, stable_ms=60 min_notes=3 retrigger_ms=1500
I (409) usb_midi: USB host installed, waiting for a MIDI device
I (2345) usb_midi: device 0499:1050 attached
I (2360) piano: piano connected (0499:1050)
I (5012) piano: chord Cmaj7      (4 notes, vel 78)
```

Every recognised chord is logged, whether or not MQTT is reachable, so this
also works as a tuning aid: play a chord, see what the firmware calls it, put
that name into `triggers.json`.

To attach without resetting (so you do not lose the current state), pass
`--no-reset`:

```bash
~/.esptool-venv/bin/python tools/monitor.py --no-reset
```

### Watching MQTT

Once the broker is configured, this shows everything the bridge publishes:

```bash
mosquitto_sub -h 192.168.1.50 -v -t 'piano/#'
```

If `mosquitto_sub` is not installed: `brew install mosquitto`.

### The underlying commands

If you would rather not use `make`:

```bash
# build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v5.5 idf.py build

# flash
cd build && ~/.esptool-venv/bin/esptool --chip esp32s3 \
    --port /dev/cu.usbmodem5C941343941 -b 460800 \
    --before default-reset --after hard-reset write-flash "@flash_args"

# log
~/.esptool-venv/bin/python tools/monitor.py
```

### With a local ESP-IDF instead

If you do install ESP-IDF v5.3 or newer natively, the normal workflow applies
and you do not need the container, the venv or the Makefile:

```bash
. $IDF_PATH/export.sh
idf.py build flash monitor
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

The chord detector has no ESP-IDF dependencies and is tested on the host, so
this needs neither the container nor the board:

```bash
make test
```

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `waiting for a MIDI device` forever, nothing on connect | No VBUS on the USB-A breakout. Check the 5 V jumper, and that the board itself is powered through the `UART` socket. |
| Device attaches, then `no USB MIDI interface found` | Something other than the piano is plugged in, or D+/D− are swapped. |
| Enumerates, then disconnects or stalls | D+/D− not twisted together, or the wires are long. Twist the pair first, shorten second. |
| Chords are recognised but Home Assistant shows nothing | Broker address or credentials; run `make monitor` and look for the `mqtt` tag. |
| Edited `credentials.defaults` but the log still says no SSID | A stale `sdkconfig` is overriding it. `make reconfigure && make build`. |
| `make monitor` shows nothing at all | Cable is in the socket marked `USB` instead of `UART`. Check with `make port`. |
| Rolled chords fire the wrong trigger | Raise `stable_ms` in `triggers.json`. |
| A trigger fires repeatedly | Raise `retrigger_ms`. |
