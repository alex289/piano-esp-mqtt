# Home Assistant

The firmware publishes MQTT discovery messages, so nothing has to be added to
`configuration.yaml`. The only prerequisite is a working **MQTT integration**
(Mosquitto add-on or any other broker).

## Entities

After the first connection a device **"Yamaha P-115"** appears under
*Settings → Devices & Services → MQTT*, with:

| Entity | Type | Meaning |
|---|---|---|
| `sensor.yamaha_p_115_akkord` | sensor | The chord most recently recognised, e.g. `Cmaj7` or `Am7/C`. Attributes hold the root, quality, bass, MIDI note numbers and velocity. |
| `binary_sensor.yamaha_p_115_spielt` | binary sensor | `on` while at least one key is held. |
| `binary_sensor.yamaha_p_115_usb_verbindung` | binary sensor | `on` while the piano is enumerated on the USB port. |

The chord sensor is retained and deliberately *keeps* its last value when you
lift your hands — use the "Spielt" sensor to tell playing from silence.

## Triggers

Every entry in `main/triggers.json` is published as an MQTT **device trigger**.
In the automation editor pick *Add trigger → Device → Yamaha P-115*, and the
configured chords are listed by name.

The YAML equivalent:

```yaml
automation:
  - alias: "Licht per Akkord"
    trigger:
      - platform: device
        domain: mqtt
        device_id: <taken from the UI>
        type: chord_played
        subtype: "Licht an"
    action:
      - service: light.turn_on
        target:
          entity_id: light.wohnzimmer
```

If you would rather not use device triggers, subscribe to the raw topic
instead — it carries the trigger id, the chord and the velocity:

```yaml
automation:
  - alias: "Licht per Akkord (MQTT)"
    trigger:
      - platform: mqtt
        topic: "piano/trigger"
        value_template: "{{ value_json.id }}"
        payload: "licht_an"
    action:
      - service: light.turn_on
        target:
          entity_id: light.wohnzimmer
```

Velocity is published with every trigger, so you can make an automation react
to how hard the chord was played:

```yaml
      - condition: template
        value_template: "{{ trigger.payload_json.velocity > 90 }}"
```

## Topics

| Topic | Retained | Payload |
|---|---|---|
| `piano/status` | yes | `online` / `offline` (last will) |
| `piano/chord` | yes | `{"name":"Cmaj7","base_name":"Cmaj7","identified":true,"root":"C","quality":"maj7","bass":"C","velocity":78,"notes":[60,64,67,71]}` |
| `piano/trigger` | no | `{"id":"licht_an","name":"Licht an","chord":"Cmaj7","velocity":78}` |
| `piano/playing` | yes | `ON` / `OFF` |
| `piano/usb` | yes | `ON` / `OFF` |
| `piano/midi` | no | raw MIDI, only when `PIANO_PUBLISH_RAW_MIDI` is enabled |

The base topic, the discovery prefix and the device id are all configurable via
`idf.py menuconfig` → *Piano MQTT Bridge*.

## Watching what the piano sends

```bash
mosquitto_sub -h <broker> -v -t 'piano/#'
```

Add `-u <user> -P <pass>` if your broker requires authentication; a Mosquitto
started with `mosquitto-no-auth.conf` does not.

Play chords and the recognised names show up immediately. This is the quickest
way to find out what to put into `triggers.json`.

## Removing the device

Discovery messages are retained, so the device survives a firmware wipe. To
clear it out:

```bash
mosquitto_sub -h <broker> -v \
  -t 'homeassistant/+/piano_p115/#' --remove-retained --retained-only
```
