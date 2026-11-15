<img src="logo.png" alt="Sensmos" height="80">

# Sensmos — ESPHome

**Publish any ESPHome sensor to the [Sensmos](https://sensmos.com) live map — no wallet, no signup, no firmware to write. Add a few lines, pick a passkey, done.**

> Your device shows up as a **software node** (purple) on [sensmos.com/map](https://sensmos.com/map/): live values, history, charts and an optional heatmap layer. It never touches the GALU economy — software nodes are data + map only.

## How it works

You add a `sensmos:` block with a **passkey** and a list of `sensor id → entity name` mappings. Every `update_interval` the component reads those sensors and POSTs them to the Sensmos ingest endpoint over HTTPS. The first push auto-registers your node (identity = `sha256(key)`), later pushes overwrite. Location comes from the optional `lat`/`lon`, or is resolved from your IP (GeoIP).

## Install

```yaml
external_components:
  - source: github://Galusz/sensmos-esphome@main
```

## Configure

```yaml
sensmos:
  key: !secret sensmos_key      # passkey, min 32 chars — this IS your node identity
  update_interval: 60s          # min 20s (the server rate-limits faster pushes)
  # insecure: true              # plain HTTP, no TLS — for low-RAM nodes (e.g. heavy BLE) where HTTPS fails
  # lat/lon optional — without them the server uses GeoIP
  # lat: 52.2297
  # lon: 21.0122
  # label: "My battery"
  sensors:
    - id: my_soc_sensor         # an existing ESPHome sensor's id
      entity: pub.batt_soc      # the Sensmos entity name
    - id: my_voltage_sensor
      entity: pub.batt_v
```

Generate a passkey (treat it like a password):

```bash
openssl rand -hex 24      # 48 hex chars
```

### Entity names

Pick **`pub.<native>`** to get a map category + heatmap, or **`own.<anything>`** for free-form data (shown on the node, no heatmap, e.g. `own.boiler_temp`).

Full native list:

| Category | Entities |
|----------|----------|
| ⚡ **PWR** (power) | `batt_soc` `batt_v` `batt_power` `load_power` `pv_power` `pv_daily` `grid_v` `grid_freq` `grid_import` `grid_export` `ev_charge` |
| 🌿 **ENV** (environment) | `temp_out` `humidity_out` `pressure` `co2` `pm25` `pm10` `noise_db` `uv_index` `rain_mm` |
| 🏠 **LIF** (home) | `temp_in` `humidity_in` `motion` `presence` `door` `window` `steps` `sleep_score` `uptime_s` `wifi_nets` `wifi_rssi` |

## Examples

### Air quality (smog) — PMS5003

Maps to native `pub.pm25` / `pub.pm10`, so it joins the air-quality heatmap on the map.

```yaml
external_components:
  - source: github://Galusz/sensmos-esphome@main

uart:
  rx_pin: GPIO16
  tx_pin: GPIO17
  baud_rate: 9600

sensor:
  - platform: pmsx003
    type: PMSX003
    pm_2_5:
      name: "PM2.5"
      id: pm25
    pm_10_0:
      name: "PM10"
      id: pm10

sensmos:
  key: !secret sensmos_key
  sensors:
    - id: pm25
      entity: pub.pm25
    - id: pm10
      entity: pub.pm10
```

### Minimal — no extra hardware

Just the ESP's own uptime + WiFi signal, to see a node appear on the map in a minute.

```yaml
external_components:
  - source: github://Galusz/sensmos-esphome@main

sensor:
  - platform: uptime
    name: "Uptime"
    id: dev_uptime
  - platform: wifi_signal
    name: "WiFi signal"
    id: dev_rssi

sensmos:
  key: !secret sensmos_key
  label: "My ESP"
  sensors:
    - id: dev_uptime
      entity: pub.uptime_s
    - id: dev_rssi
      entity: pub.wifi_rssi
```

And in `secrets.yaml`:

```yaml
sensmos_key: "paste-your-48-char-passkey-here-xxxxxxxxxxxx"
```

> Got a battery/BMS, solar inverter, weather station…? Same pattern — give the sensors an `id:` and map them to `pub.batt_soc`, `pub.pv_power`, `pub.temp_out`, etc.

## LoRa sensor without Wi-Fi (`sensmos_lora`)

Put a sensor where Wi-Fi does not reach (garage, basement, allotment, septic tank) and still get it in Home Assistant. The `sensmos_lora` component sends your ESPHome readings **by radio** on the Sensmos home channel. Any Sensmos node in range hears the frame, the network delivers it to **your Hub or node**, and that turns it into entities `sensor.sensmos_lora_<sub>_<key>` in Home Assistant. The payload is encrypted end-to-end with your key phrase (AES-256-CTR, the same frame format as the node firmware); relaying nodes and the server only see ciphertext.

Needs a board with an **SX1262** radio (Heltec V3, XIAO ESP32-S3 + SX1262, LilyGO T3-S3, RAK3312…) and ESPHome 2025.7 or newer (built-in `sx126x` component).

```yaml
external_components:
  - source: github://sensmos-network/sensmos-esphome@main
    components: [sensmos_lora]

# Sensmos home channel (EU868). Keep these values or the nodes will not hear you.
sx126x:
  # cs/rst/busy/dio1 pins, hw_version, rf_switch, tcxo_voltage: see your board
  modulation: LORA
  frequency: 868.1MHz
  bandwidth: 125_0kHz
  spreading_factor: 11
  coding_rate: CR_4_5
  sync_value: [0x34, 0x44]
  crc_enable: true
  pa_power: 14

sensmos_lora:
  hub: "a1b2c3d4"              # first 8 hex chars of your Hub / node id (shown in the app and in the Hub log)
  sub: 1                       # sensor number 1-255 -> sensor.sensmos_lora_1_*
  key: !secret sensmos_lora_key # same phrase as the Hub option lora_key (or the node's LoRa key)
  update_interval: 10min       # min 2min
  sensors:
    - sensor: temperature
      key: t                   # t, h, p, bat, v, co2, lux, pm25, pm10 get units in HA
  binary_sensors:
    - sensor: door
      key: door                # a state change is sent right away (at most every 30 s)
```

Full working example for the Heltec V3: [`examples/lora-heltec-v3.yaml`](examples/lora-heltec-v3.yaml).

- **Payload:** `t=21.5;h=40;door=1`, up to 128 bytes (readings that do not fit are skipped). `decimals:` per sensor (default 1).
- **Airtime:** at SF11 a typical frame is about 1 s on air. The 868.0–868.6 MHz band allows 1% (36 s per hour), so the component keeps its own budget (`max_airtime_per_hour`, default 30 s) and skips a send rather than break the limit.
- **Radio check:** at compile time the component refuses an `sx126x` config that is not on the home channel or above 14 dBm. `check_radio: false` turns that off (for tests only).
- **Send on demand:** `- sensmos_lora.send:` in any automation, optionally with `extra: "alarm=1"`.
- **Reception:** the frame needs a Sensmos node in range listening on the home channel. Nodes in Point mode and LoRaWAN gateways do that all the time; other nodes rotate channels and hear it part of the time.
- **Tests:** `node --test tests/frame.test.js` builds the frame in C++ and decodes it with the Hub's own code (needs `g++`, OpenSSL and `sensmos-hub` checked out next to this repo).

### Switches over radio

The other direction: Home Assistant switches a relay on a board with no Wi-Fi (gate, pump, cabin heating). Add `switches:` and the board keeps listening on the home channel. Your Sensmos **node with a LoRa radio** (the one in `hub:`) sends the command; a Hub has no radio and cannot. Each command carries a counter `c=` that must grow, so a recorded frame cannot be replayed, and the board answers right away with its new state. Full example: [`examples/lora-przelacznik.yaml`](examples/lora-przelacznik.yaml).

```yaml
sensmos_lora:
  hub: "a1b2c3d4"     # your node with a radio
  sub: 5
  key: !secret sensmos_lora_key
  switches:
    - switch: relay
      key: relay      # command relay=1 / relay=0 / relay=toggle
```

In Home Assistant (Sensmos integration with that node), a template switch:

```yaml
template:
  - switch:
      - name: Gate
        state: "{{ 'relay=1' in (states('sensor.home_node_lora_data_frame_sub_5') or '').split(';') }}"
        turn_on:
          - action: sensmos.lora_send
            data: { dst: a1b2c3d4, sub: 5, aes: true, payload: "c={{ now().timestamp() | int }};relay=1" }
        turn_off:
          - action: sensmos.lora_send
            data: { dst: a1b2c3d4, sub: 5, aes: true, payload: "c={{ now().timestamp() | int }};relay=0" }
```

`dst` is the node's own id8: the node then sends the frame by radio to its own sensor `sub`. Each command is about 1 s on air and counts toward the node's 1% budget, so this is for a few switches a day, not dimming lights.

### Recipe: garage / allotment guard

[`examples/lora-straznik-garazu.yaml`](examples/lora-straznik-garazu.yaml): a door reed switch, a water-leak probe, a DS18B20 for frost and the battery voltage, all on one Heltec V3, no Wi-Fi on site. In Home Assistant you get `sensor.sensmos_lora_2_door`, `…_leak`, `…_t`, `…_v`, and the Hub adds `binary_sensor.sensmos_lora_2_silent`, which turns `on` when the sensor goes quiet (no frame for 3 normal intervals, at least 1 hour). That is the alarm for a dead battery, a stolen board or a cut cable. The Hub also fires `sensmos_lora_silent` and, on the first frame afterwards, `sensmos_lora_back`.

```yaml
# Home Assistant automations (replace notify.mobile_app_phone with your own notifier)
automation:
  - alias: Garage door opened
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_2_door, above: 0.5 }
    action: { service: notify.mobile_app_phone, data: { message: "Garage door is open" } }
  - alias: Water on the garage floor
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_2_leak, above: 0.5 }
    action: { service: notify.mobile_app_phone, data: { message: "Water on the garage floor" } }
  - alias: Frost in the garage
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_2_t, below: 1 }
    action: { service: notify.mobile_app_phone, data: { message: "Garage below 1 °C" } }
  - alias: Garage sensor went silent
    trigger: { platform: event, event_type: sensmos_lora_silent, event_data: { sub: 2 } }
    action: { service: notify.mobile_app_phone, data: { message: "Garage sensor has been silent for {{ (trigger.event.data.silent_s / 60) | int }} min" } }
```

The silence alarm only means something if a node keeps listening: put one Sensmos node in range in **Point mode** (app → node → LoRa → frame reception). A node in the default scanner mode hears the home channel only part of each hour.

### Recipe: tank level (septic tank, rainwater, well)

[`examples/lora-szambo.yaml`](examples/lora-szambo.yaml): a waterproof ultrasonic sensor (JSN-SR04T) in the lid measures the distance to the surface, and the board sends the level in %, litres, the raw distance and the battery every 30 minutes. Enter three numbers from a tape measure (`empty_cm`, `full_cm`, `liters`) and the shape: `0` for a box or upright cylinder, `1` for a cylinder lying on its side, where litres come from the circle segment. You get `sensor.sensmos_lora_4_lvl`, `…_l`, `…_d` and `…_v`.

```yaml
automation:
  - alias: Septic tank 80%
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_4_lvl, above: 80 }
    action: { service: notify.mobile_app_phone, data: { message: "Septic tank is {{ states('sensor.sensmos_lora_4_lvl') }}% full, time to book the truck" } }
```

### Recipe: meters in the basement

[`examples/lora-liczniki.yaml`](examples/lora-liczniki.yaml): a light sensor module over the blinking LED of the electricity meter and the pulse output (reed switch) of the water meter, both counted by the board, which sends every 5 minutes. Enter the pulses per kWh and litres per pulse from the plates, and the readings on the day you install it (`e_start`, `w_start`), so Home Assistant shows the same numbers as the meters. Counted pulses survive a reboot. With Sensmos Hub 0.2.0-beta6 or newer, `sensor.sensmos_lora_5_kwh` and `…_m3` are counters that go straight into the **Energy** dashboard; `…_w` is the power right now and `…_lh` the water flow. The board counts all the time, so power it from a USB charger, not a battery.

```yaml
automation:
  - alias: Water running at night
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_5_lh, above: 0, for: "02:00:00" }
    condition: { condition: time, after: "01:00:00", before: "05:00:00" }
    action: { service: notify.mobile_app_phone, data: { message: "Water has been running for 2 hours at night, check for a leak" } }
```

Meters that only talk wM-Bus radio (many new water and heat meters) need a second radio and are not in this recipe yet.

### Recipe: car parked outside

[`examples/lora-auto.yaml`](examples/lora-auto.yaml): a board in the car, powered from **pin 16 of the OBD2 port** (permanent 12 V; pins 4 and 5 are ground) through a step-down converter that draws under 1 mA with no load (not an LM2596 module, those draw several mA on their own) and a 1 A fuse. Only power is taken from the port; the car's data bus is not touched. A 100 kΩ / 22 kΩ divider on GPIO4 measures the battery, and an SW-420 shock sensor on GPIO7 wakes the board. It sleeps, wakes every 30 minutes or on a shock, sends one frame and sleeps again; below `low_v` (11.8 V) it sleeps for 6 hours to protect the battery. You get `sensor.sensmos_lora_6_v12`, `…_mv` (1 = woken by a shock) and `…_v`, and the hub's `binary_sensor.sensmos_lora_6_silent` turns on when the car leaves the range or the board is unplugged.

```yaml
automation:
  - alias: Someone is moving the car
    trigger: { platform: state, entity_id: sensor.sensmos_lora_6_mv, to: "1" }
    condition: { condition: state, entity_id: input_boolean.i_am_driving, state: "off" }
    action: { service: notify.mobile_app_phone, data: { message: "Shock at the car, battery {{ states('sensor.sensmos_lora_6_v12') }} V" } }
  - alias: Car battery low
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_6_v12, below: 11.9 }
    action: { service: notify.mobile_app_phone, data: { message: "Car battery is at {{ states('sensor.sensmos_lora_6_v12') }} V" } }
```

The car body blocks radio, so put the antenna under the windscreen. This is a notification, not a car alarm or a tracker: a frame can be lost, and when the car drives away, it is out of range.

### Recipe: camper, trailer, boat

[`examples/lora-kamper.yaml`](examples/lora-kamper.yaml): a winter-storage profile. A BME280 measures temperature, humidity and pressure, a reed switch watches the door (or a locker) and wakes the board when it opens, a leak probe sits in the bilge or under the water tank, and a divider measures the leisure battery. The board sleeps, sends every 30 minutes and on every door opening, and sleeps 6 hours when the battery is below 11.8 V. Power comes from the leisure battery through a low-quiescent step-down converter, or from an 18650 cell. You get `sensor.sensmos_lora_7_t`, `…_h`, `…_p`, `…_v12`, `…_door`, `…_leak` and `…_v`.

```yaml
automation:
  - alias: Camper frost
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_7_t, below: 2 }
    action: { service: notify.mobile_app_phone, data: { message: "Camper is at {{ states('sensor.sensmos_lora_7_t') }} °C, drain the water" } }
  - alias: Camper damp
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_7_h, above: 75, for: "48:00:00" }
    action: { service: notify.mobile_app_phone, data: { message: "Humidity in the camper has been above 75% for 2 days" } }
  - alias: Camper door or water
    trigger:
      - { platform: state, entity_id: sensor.sensmos_lora_7_door, to: "1" }
      - { platform: state, entity_id: sensor.sensmos_lora_7_leak, to: "1" }
    action: { service: notify.mobile_app_phone, data: { message: "Camper: {{ trigger.to_state.name }} changed to 1" } }
```

Storage yards are often out of range of any node. Check first with a node in Point mode near the yard, or ask a neighbour there to run one.

### Recipe: mailbox and garden gate

[`examples/lora-skrzynka.yaml`](examples/lora-skrzynka.yaml): the board sleeps on battery and wakes only when the letter flap (or the gate) opens, when the collection door opens, or once every 12 hours as a heartbeat. It sends one frame and goes back to sleep. Use **normally closed** reed switches between 3.3 V and GPIO6/GPIO7 with a 100 kΩ resistor to GND: with the flap shut the reeds draw no current, so the battery only feeds the board's own sleep current. A normally open reed with a pull-up also works, but draws current all the time. If the flap stays open (a parcel), the board stops waking on that pin and checks again every 30 minutes, so it does not drain the battery. For a gate, wire one reed to GPIO6. You get `sensor.sensmos_lora_8_mail` (1 = mail waiting, 0 = collected), `…_ev` (what woke it: 1 flap or gate, 2 collection door, 0 heartbeat) and `…_v`. Hub 0.2.0-beta6 or newer counts the 12-hour heartbeat as the usual interval, so the silent alarm comes after 36 hours, not one hour after two quick frames.

```yaml
automation:
  - alias: Mail arrived
    trigger: { platform: state, entity_id: sensor.sensmos_lora_8_ev, attribute: ts }   # every frame, also the second letter
    condition: { condition: state, entity_id: sensor.sensmos_lora_8_ev, state: "1" }
    action: { service: notify.mobile_app_phone, data: { message: "You've got mail" } }
  - alias: Mailbox battery low
    trigger: { platform: numeric_state, entity_id: sensor.sensmos_lora_8_v, below: 3.4 }
    action: { service: notify.mobile_app_phone, data: { message: "Mailbox battery is low ({{ states('sensor.sensmos_lora_8_v') }} V)" } }
```

For the gate, change the message to "Someone opened the gate" and give the board its own `sub`.

### Recipe: alarm button for your family

[`examples/lora-przycisk.yaml`](examples/lora-przycisk.yaml): a big mushroom button by a bed, in a garden shed or a workshop, where there is no Wi-Fi. Hold it for 2 s and the board sends `alarm=1`, repeating it every minute (at most 5 times) until someone accepts. Two short presses send `alarm=0` (all fine). The board also has a switch `ack`: the Home Assistant blueprint [`przycisk_alarmu`](https://github.com/sensmos-network/sensmos-homeassistant/blob/main/blueprints/automation/sensmos/przycisk_alarmu.yaml) notifies the phones you choose with an **I'm on it** action, escalates to a second group, and sends `ack=1` back by your node's radio: the LED stays on and a buzzer beeps, so the person knows help is coming. Power it from USB, because the board keeps listening for the confirmation. It is built from stock ESPHome parts (`binary_sensor` with `on_multi_click`, `script`, `status_led`, a template `switch`) plus `sensmos_lora`. This is help from family and neighbours, not an emergency service: it does not replace 112.

### Recipe: greenhouse and garden watering

[`examples/lora-szklarnia.yaml`](examples/lora-szklarnia.yaml): a capacitive soil moisture sensor (in %, calibrated with two readings: dry air and a glass of water), a DS18B20 and a 12 V normally closed solenoid valve on a relay, all on stock ESPHome parts. Readings go to Home Assistant at home, and the valve is a `switches:` entry, so HA opens it by your node's radio, as in [Switches over radio](#switches-over-radio). The board closes the valve by itself after `max_open` (20 min), even if the radio goes quiet, and always starts closed. It keeps listening, so it needs steady power (mains or a solar panel with a battery). You get `sensor.sensmos_lora_10_soil`, `…_t`, and the valve state `valve=1/0` in `sensor.<node>_lora_data_frame_sub_10`.

```yaml
template:
  - switch:
      - name: Greenhouse valve
        state: "{{ 'valve=1' in (states('sensor.home_node_lora_data_frame_sub_10') or '').split(';') }}"
        turn_on:
          - action: sensmos.lora_send
            data: { dst: a1b2c3d4, sub: 10, aes: true, payload: "c={{ now().timestamp() | int }};valve=1" }
        turn_off:
          - action: sensmos.lora_send
            data: { dst: a1b2c3d4, sub: 10, aes: true, payload: "c={{ now().timestamp() | int }};valve=0" }
automation:
  - alias: Water the greenhouse when dry
    trigger: { platform: time, at: "06:30:00" }
    condition: { condition: numeric_state, entity_id: sensor.sensmos_lora_10_soil, below: 35 }
    action:
      - action: switch.turn_on
        target: { entity_id: switch.greenhouse_valve }
      - delay: "00:10:00"
      - action: switch.turn_off
        target: { entity_id: switch.greenhouse_valve }
```

Watering in the morning, once a day, keeps radio use to two commands a day. Hub 0.2.0-beta6 or newer gives `soil` the moisture device class.

### Recipe: apiary (hive scale)

[`examples/lora-pasieka.yaml`](examples/lora-pasieka.yaml): a load cell under the hive with an HX711 (the board powers it only while weighing), a DS18B20 in the brood nest (about 34–35 °C in season means the colony is alive) and an SW-420 vibration sensor that wakes the board when someone lifts or tips the hive. The board sleeps on an 18650 cell, best with a small 5 V solar panel, wakes every 30 minutes, weighs and sends one frame. Calibrate with two raw readings (empty and a known weight) and keep the tare of the empty hive in Home Assistant. You get `sensor.sensmos_lora_11_kg`, `…_t`, `…_mv` (1 = woken by a shock) and `…_v`. Daily gain and a swarm alert come from stock HA integrations:

```yaml
sensor:
  - platform: statistics
    name: Hive gain 24h
    entity_id: sensor.sensmos_lora_11_kg
    state_characteristic: change
    max_age: { hours: 24 }
  - platform: statistics
    name: Hive change 1h
    entity_id: sensor.sensmos_lora_11_kg
    state_characteristic: change
    max_age: { hours: 1 }
automation:
  - alias: Possible swarm
    trigger: { platform: numeric_state, entity_id: sensor.hive_change_1h, below: -1.5 }
    action: { service: notify.mobile_app_phone, data: { message: "The hive lost {{ states('sensor.hive_change_1h') }} kg in an hour: a swarm?" } }
  - alias: Hive moved
    trigger: { platform: state, entity_id: sensor.sensmos_lora_11_mv, to: "1" }
    action: { service: notify.mobile_app_phone, data: { message: "Someone is moving the hive" } }
```

Hub 0.2.0-beta6 or newer gives `kg` the weight device class.

### Recipe: cold room, display fridge, freezer

[`examples/lora-chlodnia.yaml`](examples/lora-chlodnia.yaml): two waterproof DS18B20 probes on one bus (a cold room `t` and a freezer `t2`) and a door reed, every 5 minutes and on every door change, USB-powered from the socket next to the appliance, no Wi-Fi in the back room. Keep the board outside the cold. In Home Assistant the Sensmos blueprint [`chlodnia`](https://github.com/sensmos-network/sensmos-homeassistant/blob/main/blueprints/automation/sensmos/chlodnia.yaml) raises the alarms (too warm for longer than a door opening, back to normal, no reading), and the log is HA's own history with CSV download. For HACCP records compare the probes with a calibrated thermometer. Hub 0.2.0-beta6 or newer gives `t2` the temperature class.

### Recipe: electric fence monitor

[`examples/lora-pastuch.yaml`](examples/lora-pastuch.yaml): is the fence still pulsing? The board **never touches the fence wire**: about 1 m of insulated wire laid along the fence (on insulators, 5–10 cm away) picks up each energizer pulse inductively, through 1 MΩ, a 100 kΩ resistor to GND and clamp diodes, into ESPHome's stock `pulse_counter`. The board sleeps on an 18650 cell (ideally with a small solar panel), wakes every 10 minutes, counts pulses for 10 seconds and sends pulses per minute (`ppm`, usually 40–60) and `fence` (1 = pulsing). It tells you whether the energizer works, not how many kilovolts the fence has: check that with a fence tester. You get `sensor.sensmos_lora_13_ppm`, `…_fence` and `…_v`.

```yaml
automation:
  - alias: Fence is not pulsing
    trigger: { platform: state, entity_id: sensor.sensmos_lora_13_fence, to: "0", for: "00:15:00" }
    action: { service: notify.mobile_app_phone, data: { message: "The electric fence stopped pulsing ({{ states('sensor.sensmos_lora_13_ppm') }}/min)" } }
```

## Read another node's data (preview)

The other direction: pull a value another node publishes and expose it as a normal ESPHome sensor — show a neighbour's air quality on a local display, drive an automation, whatever. It's a **preview, not realtime**: it polls the public map data on a long interval (default 5 min). No wallet, no subscription.

```yaml
external_components:
  - source: github://Galusz/sensmos-esphome@main

sensor:
  - platform: sensmos
    name: "Neighbour PM2.5"
    node: "0123…64-hex…cdef"   # the target node's device id (map → node popup → copy)
    entity: pub.pm25            # which published entity to read
    update_interval: 5min       # how often to poll (keep it long — it's a preview)
```

Each `- platform: sensmos` entry = one remote entity → one ESPHome sensor. Add more entries (same or different `node`) for more values. Works for any node, real or software. The fetch runs off the main loop, so it won't stall BLE.

## Notes

- **Frameworks:** works on both `esp-idf` (uses the cert bundle) and `arduino`.
- **Timeout / pruning:** a software node goes offline after 48 h without a push and reappears automatically on the next one. Online/offline status is shown on the map.
- **Up to 50 entities** per node.
- **Non-blocking:** both publish (POST) and read (GET) run the HTTPS call in a separate FreeRTOS task, so they don't stall the main loop or BLE. Still, keep intervals sane (publish ≥ 60 s, read ≥ a few min).
- **Low-RAM nodes:** on a memory-tight node (e.g. a big BLE stack like a BMS), the HTTPS handshake can fail with mbedTLS alloc errors (it must buffer the server cert). Add **`insecure: true`** to the `sensmos:` block and/or to a `- platform: sensmos` reader → it uses plain **HTTP** (no TLS, almost no RAM). It's telemetry and software nodes have no economy, so the risk is low.

## Part of the Sensmos project

| | |
|---|---|
| 🌐 Website | https://sensmos.com |
| 🗺️ Live map | https://sensmos.com/map/ |
| 📱 App | https://github.com/sensmos-network/sensmos-app |
| 🔌 Firmware | https://github.com/sensmos-network/sensmos-firmware |
| 🏠 Home Assistant | https://github.com/sensmos-network/sensmos-homeassistant |
| 🧩 Hub (node in Docker / HA add-on) | https://github.com/sensmos-network/sensmos-hub |
| 📜 Protocol | https://github.com/sensmos-network/sensmos-protocol |
| 💬 Discord | https://discord.gg/ukea386Kqx |

GALU runs on Polygon. © 2026 Sensmos.
