"""Sensmos LoRa: czujnik ESPHome nadający odczyty radiem przez sieć Sensmos.

Treść „t=21.5;h=40;bat=87” idzie ramką DATA (0xE0 0x02, AES-256-CTR) na kanale domowym
Sensmos. Dowolny nod Sensmos w zasięgu ją słyszy, serwer dowozi ją do Huba lub noda
o podanym id8, a ten robi z niej encje w Home Assistant.
"""

import inspect

from esphome import automation
import esphome.codegen as cg
from esphome.components import binary_sensor, sensor, switch, sx126x
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_KEY, CONF_SENSORS, CONF_UPDATE_INTERVAL
import esphome.final_validate as fv

CODEOWNERS = ["@Galusz"]
DEPENDENCIES = ["sx126x"]

CONF_RADIO_ID = "radio_id"
CONF_HUB = "hub"
CONF_SUB = "sub"
CONF_OPEN = "open"
CONF_BINARY_SENSORS = "binary_sensors"
CONF_SENSOR = "sensor"
CONF_DECIMALS = "decimals"
CONF_ON_CHANGE = "send_on_change"
CONF_AIRTIME = "max_airtime_per_hour"
CONF_CHECK_RADIO = "check_radio"
CONF_EXTRA = "extra"
CONF_SWITCHES = "switches"
CONF_SWITCH = "switch"

sensmos_lora_ns = cg.esphome_ns.namespace("sensmos_lora")
SensmosLora = sensmos_lora_ns.class_("SensmosLora", cg.PollingComponent)
SendAction = sensmos_lora_ns.class_("SendAction", automation.Action)

# Kanał domowy Sensmos w Europie (backend lora_regions.js homeChannel, firmware komunikatora):
# 868.1 MHz, BW 125 kHz, SF11, CR 4/5, publiczny sync word 0x34 (w SX126x: 0x34 0x44), CRC włączone.
HOME = {
    "frequency": 868100000,
    "bandwidth": "125_0kHz",
    "spreading_factor": 11,
    "coding_rate": "CR_4_5",
    "sync_value": [0x34, 0x44],
}
EU_MAX_DBM = 14  # 25 mW ERP w podpaśmie 868.0-868.6 MHz


def _key_name(value):
    value = cv.string(value)
    if not value or len(value) > 16 or not value[0].isalpha() or not all(
        c.isalnum() or c == "_" for c in value
    ):
        raise cv.Invalid(
            "klucz: litera na początku, potem litery, cyfry lub _, najwyżej 16 znaków (np. t, h, bat, door)"
        )
    return value


def _hub_id8(value):
    value = cv.string(value).strip().lower()
    if len(value) != 8 or any(c not in "0123456789abcdef" for c in value):
        raise cv.Invalid(
            "hub: pierwsze 8 znaków hex identyfikatora Huba albo noda (widać je w apce i w logu Huba)"
        )
    return value


SENSOR_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR): cv.use_id(sensor.Sensor),
        cv.Required(CONF_KEY): _key_name,
        cv.Optional(CONF_DECIMALS, default=1): cv.int_range(min=0, max=4),
    }
)

BINARY_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        cv.Required(CONF_KEY): _key_name,
        cv.Optional(CONF_ON_CHANGE, default=True): cv.boolean,
    }
)


SWITCH_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_KEY): _key_name,
    }
)


def _need_key(config):
    if CONF_KEY not in config and not config[CONF_OPEN]:
        raise cv.Invalid(
            "podaj key (tę samą frazę co opcja lora_key w Hubie) albo ustaw open: true dla ramek jawnych"
        )
    if config.get(CONF_SWITCHES) and CONF_KEY not in config:
        raise cv.Invalid("switches: przełączniki przez radio wymagają key (polecenia muszą być szyfrowane)")
    if not config.get(CONF_SENSORS) and not config.get(CONF_BINARY_SENSORS) and not config.get(CONF_SWITCHES):
        raise cv.Invalid("dodaj co najmniej jeden czujnik w sensors, binary_sensors albo switches")
    # SF11 to ~1 s w eterze na ramkę; częściej niż co 2 min szybko kończy się limit 1%.
    if config[CONF_UPDATE_INTERVAL].total_milliseconds < 120000:
        raise cv.Invalid("update_interval: co najmniej 2min (limit czasu nadawania w paśmie 868 MHz)")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SensmosLora),
            cv.GenerateID(CONF_RADIO_ID): cv.use_id(sx126x.SX126x),
            cv.Required(CONF_HUB): _hub_id8,
            cv.Optional(CONF_SUB, default=1): cv.int_range(min=1, max=255),
            cv.Optional(CONF_KEY): cv.All(cv.string, cv.Length(min=8)),
            cv.Optional(CONF_OPEN, default=False): cv.boolean,
            cv.Optional(CONF_SENSORS, default=[]): cv.All(
                cv.ensure_list(SENSOR_SCHEMA), cv.Length(max=16)
            ),
            cv.Optional(CONF_BINARY_SENSORS, default=[]): cv.All(
                cv.ensure_list(BINARY_SCHEMA), cv.Length(max=16)
            ),
            # Limit EU868 w 868.0-868.6 MHz to 1% = 36 s na godzinę. Zostawiamy zapas.
            # Przełączniki sterowane z HA przez radio noda-bazy (sensmos.lora_send). Płytka słucha stale.
            cv.Optional(CONF_SWITCHES, default=[]): cv.All(
                cv.ensure_list(SWITCH_SCHEMA), cv.Length(max=8)
            ),
            cv.Optional(CONF_AIRTIME, default="30s"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(max=cv.TimePeriod(seconds=36)),
            ),
            cv.Optional(CONF_CHECK_RADIO, default=True): cv.boolean,
        }
    ).extend(cv.polling_component_schema("10min")),
    _need_key,
)


def _final_validate(config):
    if not config[CONF_CHECK_RADIO]:
        return
    full = fv.full_config.get()
    radio_id = config[CONF_RADIO_ID]
    radios = full.get("sx126x", [])
    if isinstance(radios, dict):
        radios = [radios]
    radio = next((r for r in radios if r.get(CONF_ID) == radio_id), None)
    if radio is None:
        return
    problems = []
    if str(radio.get("modulation")).upper() != "LORA":
        problems.append("modulation: LORA")
    if int(radio.get("frequency", 0)) != HOME["frequency"]:
        problems.append("frequency: 868.1MHz")
    if str(radio.get("bandwidth")) != HOME["bandwidth"]:
        problems.append("bandwidth: 125_0kHz")
    if int(radio.get("spreading_factor", 0)) != HOME["spreading_factor"]:
        problems.append("spreading_factor: 11")
    if str(radio.get("coding_rate")) != HOME["coding_rate"]:
        problems.append("coding_rate: CR_4_5")
    if list(radio.get("sync_value", [])) != HOME["sync_value"]:
        problems.append("sync_value: [0x34, 0x44]")
    if not radio.get("crc_enable"):
        problems.append("crc_enable: true")
    if int(radio.get("payload_length", 0)) != 0:
        problems.append("payload_length: 0 (albo usuń)")
    if config.get(CONF_SWITCHES) and not radio.get("rx_start", True):
        problems.append("rx_start: true (przełączniki muszą słyszeć polecenia)")
    if int(radio.get("pa_power", 17)) > EU_MAX_DBM:
        problems.append(f"pa_power: {EU_MAX_DBM} (limit EU dla 868.1 MHz)")
    if problems:
        raise cv.Invalid(
            "Radio musi nadawać na kanale domowym Sensmos, inaczej nody go nie usłyszą. "
            "Ustaw w sx126x: " + ", ".join(problems)
            + ". (Wyłączenie sprawdzania: check_radio: false.)"
        )


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    radio = await cg.get_variable(config[CONF_RADIO_ID])
    cg.add(var.set_radio(radio))
    cg.add(var.set_dst(config[CONF_HUB]))
    cg.add(var.set_sub(config[CONF_SUB]))
    if CONF_KEY in config:
        cg.add(var.set_key(config[CONF_KEY]))
    cg.add(var.set_open(config[CONF_OPEN]))
    cg.add(var.set_airtime_budget_ms(config[CONF_AIRTIME].total_milliseconds))

    # Parametry do liczenia czasu nadawania = kanał domowy (sprawdzone w _final_validate).
    cg.add(var.set_radio_params(HOME["spreading_factor"], 125000, 5, 8))

    for item in config[CONF_SENSORS]:
        s = await cg.get_variable(item[CONF_SENSOR])
        cg.add(var.add_sensor(s, item[CONF_KEY], item[CONF_DECIMALS]))
    for item in config[CONF_BINARY_SENSORS]:
        s = await cg.get_variable(item[CONF_SENSOR])
        cg.add(var.add_binary_sensor(s, item[CONF_KEY], item[CONF_ON_CHANGE]))
    for item in config[CONF_SWITCHES]:
        s = await cg.get_variable(item[CONF_SWITCH])
        cg.add(var.add_switch(s, item[CONF_KEY]))


SEND_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(SensmosLora),
        cv.Optional(CONF_EXTRA, default=""): cv.templatable(cv.string),
    }
)


# Nowsze ESPHome chcą wiedzieć, czy akcja kończy się od razu (u nas tak); starsze nie znają tego argumentu.
_SYNC = (
    {"synchronous": True}
    if "synchronous" in inspect.signature(automation.register_action).parameters
    else {}
)


@automation.register_action("sensmos_lora.send", SendAction, SEND_SCHEMA, **_SYNC)
async def send_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    extra = await cg.templatable(config[CONF_EXTRA], args, cg.std_string)
    cg.add(var.set_extra(extra))
    return var
