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
