#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/components/sx126x/sx126x.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#include <string>
#include <vector>
#include <deque>
#include "frame.h"

namespace esphome {
namespace sensmos_lora {

// Czujnik ESPHome jako czujnik LoRa sieci Sensmos: co update_interval składa treść „t=21.5;h=40;bat=87”,
// szyfruje ją jak firmware noda i nadaje radiem SX126x na kanale domowym Sensmos (868.1 MHz, SF11).
// Ramkę słyszy dowolny nod Sensmos w zasięgu; serwer dowozi ją do Huba albo noda-adresata (hub: id8),
// a ten robi z niej encje sensor.sensmos_lora_<sub>_<klucz> w Home Assistant.
class SensmosLora : public PollingComponent {
 public:
  void set_radio(sx126x::SX126x *r) { this->radio_ = r; }
  void set_dst(const std::string &id8);
  void set_sub(uint8_t sub) { this->sub_ = sub; }
  void set_key(const std::string &phrase);
  void set_open(bool v) { this->open_ = v; }
  void set_airtime_budget_ms(uint32_t ms) { this->budget_ms_ = ms; }
  void set_radio_params(uint8_t sf, uint32_t bw_hz, uint8_t cr, uint16_t preamble) {
    this->sf_ = sf;
    this->bw_hz_ = bw_hz;
    this->cr_ = cr;
    this->preamble_ = preamble;
  }
#ifdef USE_SENSOR
  void add_sensor(sensor::Sensor *s, const std::string &key, int8_t decimals) {
    this->sensors_.push_back({s, key, decimals});
  }
#endif
#ifdef USE_BINARY_SENSOR
  void add_binary_sensor(binary_sensor::BinarySensor *s, const std::string &key, bool on_change);
#endif

  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Nadaj teraz (akcja sensmos_lora.send albo zmiana czujnika binarnego). false = brak pasma lub błąd.
  bool send_now(const std::string &extra = "");
  // Treść, która poszłaby teraz (do logów i lambd).
  std::string build_payload();

 protected:
#ifdef USE_SENSOR
  struct SensorItem {
    sensor::Sensor *s;
    std::string key;
    int8_t decimals;
  };
  std::vector<SensorItem> sensors_;
#endif
#ifdef USE_BINARY_SENSOR
  struct BinaryItem {
    binary_sensor::BinarySensor *s;
    std::string key;
  };
  std::vector<BinaryItem> binaries_;
#endif
  bool budget_ok_(uint32_t airtime_ms);

  sx126x::SX126x *radio_{nullptr};
  uint8_t dst_[4]{0, 0, 0, 0};
  bool dst_ok_{false};
  uint8_t sub_{1};
  uint8_t key_[32];
  bool key_ok_{false};
  bool open_{false};
  uint8_t sf_{11};
  uint32_t bw_hz_{125000};
  uint8_t cr_{5};
  uint16_t preamble_{8};
  uint32_t budget_ms_{30000};          // na godzinę; limit EU868 w 868.0-868.6 to 36 s (1%)
  std::deque<std::pair<uint32_t, uint32_t>> sent_;  // (millis, airtime) z ostatniej godziny
  bool pending_change_{false};
  uint32_t last_change_tx_{0};
  uint32_t sent_ok_{0}, sent_skip_{0}, sent_err_{0};
};

template<typename... Ts> class SendAction : public Action<Ts...>, public Parented<SensmosLora> {
 public:
  TEMPLATABLE_VALUE(std::string, extra)
  void play(Ts... x) override { this->parent_->send_now(this->extra_.value(x...)); }
};

}  // namespace sensmos_lora
}  // namespace esphome
