# Hardware

Board: **ESP32-S3-DevKitC-1 (N16R8)**, the diymore clone.
Piano: **Yamaha P-115**, "USB TO HOST" socket (USB type B).

The P-115 is a class-compliant USB MIDI *device*. The ESP32-S3 therefore has to
act as a USB **host**, which its USB-OTG peripheral supports.

## Why you cannot just use the native USB-C socket

The data side would be fine: the socket marked `USB` is wired to GPIO19 (D−)
and GPIO20 (D+), which is exactly where the OTG peripheral lives. The problem
is power.

* On the DevKitC-1 the native socket's **VBUS pin sits behind diode D7**. The
  diode lets 5 V flow *into* the board (so you can power it over USB), but not
  *out*. In host mode the board supplies no bus power at all.
* The P-115 powers itself from the mains, but the USB specification still
  requires a device to wait for VBUS before pulling up D+. Without 5 V on the
  bus the piano never signals its presence, so the ESP32 sees nothing at all —
  no error, just silence.

Bridging D7 would fix it, but that means soldering, and afterwards you must
never plug a PC into that socket again (5 V against 5 V). The wiring below
avoids both problems.

## What to buy

| Part | Note |
|---|---|
| USB type A **socket** breakout board, 2.54 mm pin header | ~2–3 €, search for "USB A female breakout". Make sure the header is pre-soldered. |
| USB-A to USB-B cable | A normal printer cable. |
| 4 female–female jumper wires | Both ends need sockets: the DevKitC-1 header and the breakout both present pins. Twist the data pair; see below. |
| Optional: 500 mA polyfuse | In the 5 V line, cheap insurance. |

## Wiring

Use the pins by their silkscreen labels; the numbering is the same on the
clones. GPIO19/20 are on header **J3**, `5V` and `G` on **J1**.

> **Clones: bridge `IN-OUT` first.** On the diymore/YD-style clones the pin is
> labelled `5Vin`, and out of the box it carries **no** voltage when the board
> runs from USB: a diode lets 5 V into that pin but not out of it. Next to the
> USB-C sockets there are three solder-jumper pad pairs, `RGB`, `IN-OUT` and
> `USB-OTG`, all open from the factory. Bridge **`IN-OUT`** with a blob of
> solder and `5Vin` then carries the USB 5 V. Leave the other two open.
> The USB sockets keep their own diodes, so nothing can push current back into
> the PC. The other direction is open now, though: do not connect an external
> supply to `5Vin` while USB is plugged in, or USB will back-feed it. Without this bridge the line
> probe reports `D+ and D- both low` no matter how good the wiring is.

```
USB-A breakout            ESP32-S3-DevKitC-1
-------------------------------------------
VBUS (pin 1, often "5V")  -->  5V
D-   (pin 2)              -->  19      (GPIO19, USB_D-)
D+   (pin 3)              -->  20      (GPIO20, USB_D+)
GND  (pin 4, "G")         -->  G       (GND)
```

Then: USB-A cable from the breakout to the piano's **USB TO HOST** socket, and
power the ESP32 through the socket marked **UART** (that one also carries the
serial console for `idf.py monitor`).

```
  mains ---- Yamaha P-115
                 |
            USB TO HOST (type B)
                 |
          USB-A/USB-B cable
                 |
         USB-A breakout board
            | | | |
         5V 19 20 G          (jumper wires)
            | | | |
        ESP32-S3-DevKitC-1
                 |
            USB-C "UART"  ---- 5 V power supply / PC
```

## Rules of thumb

* **Never plug anything into the socket marked `USB` while the breakout is
  connected.** Both use GPIO19/20; they cannot be active at the same time.
* **Twist the D+/D− pair together** along its whole length — wind the two
  wires around each other like a cord, about one turn per 1–2 cm, which is
  roughly how tightly real twisted-pair cable is wound. Twist only those two;
  pair VBUS and GND separately or leave them loose, but do not bundle all four,
  or the supply couples noise into the data pair. This matters more than the
  length does. Jumper wires are a poor transmission line, but the
  ESP32-S3 only speaks USB full-speed (12 Mbit/s), which tolerates far more
  than high-speed would — 20 cm of twisted jumper wire is normally fine, and
  MIDI uses a tiny fraction of that bandwidth anyway.
* If enumeration is flaky — the log repeating `device ... attached`, stalls, or
  reconnects — suspect the wiring before the firmware. Shorter wires and a
  tighter twist are the first thing to try.
* The piano draws essentially nothing from VBUS, so powering the breakout from
  the board's 5 V pin is fine. On the original DevKitC-1 that pin is fed from
  the USB sockets; on the clones only once `IN-OUT` is bridged (see above).
  Use a decent 5 V supply, not a weak phone charger.
* No multimeter? Plug a phone's charging cable into the breakout. If it does
  not start charging, there is no VBUS.

## Checking that it works

`idf.py monitor` should show, within a second of connecting the piano:

```
I (1234) usb_midi: USB host installed, waiting for a MIDI device
I (2345) usb_midi: device 0499:160f attached
I (2350) usb_midi: MIDIStreaming interface 1 alt 0
I (2355) usb_midi: streaming MIDI from EP 0x82 (mps 64)
I (2360) piano: piano connected (0499:160f)
```

`0x0499` is Yamaha's USB vendor id, `160f` is what the P-115 reports. If the
piano is connected but chords produce nothing, enable *Log every key press and
release* under *Piano MQTT Bridge* in `make menuconfig`: every key then shows
up as a `note on` / `note off` line. No such lines means the problem is on the
USB side, not in chord detection or MQTT.

If nothing attaches, turn on the line probe: *Check the USB data lines at
boot* under *Piano MQTT Bridge* in `make menuconfig`. It is off by default
because it is read once at boot, before the USB host takes over the pins, so
it only means something when the piano is already on: connect everything,
switch the piano on, then reset the board. It looks at the bare D+/D− levels:
a powered piano pulls D+ high. If it reports both lines low, the piano is not
seeing VBUS or the cable is not plugged in — see the troubleshooting table in
the README.
