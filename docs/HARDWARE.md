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
  the board's 5 V pin is fine. That pin is fed straight from the `UART` socket,
  so use a decent 5 V supply, not a weak phone charger.

## Checking that it works

`idf.py monitor` should show, within a second of connecting the piano:

```
I (1234) usb_midi: USB host installed, waiting for a MIDI device
I (2345) usb_midi: device 0499:XXXX attached
I (2350) usb_midi: MIDIStreaming interface 1 alt 0
I (2355) usb_midi: streaming MIDI from EP 0x81 (mps 64)
I (2360) piano: piano connected (0499:XXXX)
```

`0x0499` is Yamaha's USB vendor id. If nothing appears at all, the problem is
almost always VBUS or the D+/D− pair — see the troubleshooting table in the
README.
