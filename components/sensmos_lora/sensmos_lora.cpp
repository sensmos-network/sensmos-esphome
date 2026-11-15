#include "sensmos_lora.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include <mbedtls/aes.h>
#include <mbedtls/sha256.h>
#include <cmath>
#include <cstdio>

namespace esphome {
namespace sensmos_lora {

static const char *const TAG = "sensmos_lora";
static constexpr uint32_t HOUR_MS = 3600000UL;
static constexpr uint32_t CHANGE_GAP_MS = 30000UL;  // zmiany czujnika binarnego najczęściej co 30 s

static void ctr_mbedtls(const uint8_t key[32], const uint8_t iv[16], uint8_t *buf, size_t n) {
  uint8_t ctr[16], sb[16];
  size_t off = 0;
  memcpy(ctr, iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 256);
  mbedtls_aes_crypt_ctr(&aes, n, &off, ctr, sb, buf, buf);
  mbedtls_aes_free(&aes);
}

void SensmosLora::set_dst(const std::string &id8) {
  this->dst_ok_ = id8.size() == 8 && parse_hex(id8, this->dst_, 4);
}

void SensmosLora::set_key(const std::string &phrase) {
  // Klucz = SHA256(fraza), tak samo jak w firmware noda i w opcji lora_key Huba.
  mbedtls_sha256(reinterpret_cast<const unsigned char *>(phrase.data()), phrase.size(), this->key_, 0);
  this->key_ok_ = !phrase.empty();
}

#ifdef USE_BINARY_SENSOR
void SensmosLora::add_binary_sensor(binary_sensor::BinarySensor *s, const std::string &key, bool on_change) {
  this->binaries_.push_back({s, key});
  if (on_change)
    s->add_on_state_callback([this](bool) { this->pending_change_ = true; });
}
#endif

void SensmosLora::setup() {
  if (!this->dst_ok_) {
    ESP_LOGE(TAG, "hub: potrzeba 8 znaków hex (id8 Huba albo noda)");
    this->mark_failed();
    return;
  }
  if (!this->key_ok_ && !this->open_) {
    ESP_LOGE(TAG, "key: podaj frazę klucza (tę samą co lora_key w Hubie) albo open: true");
    this->mark_failed();
  }
}

std::string SensmosLora::build_payload() {
  std::string out;
  auto add = [&out](const std::string &k, const std::string &v) {
    std::string part = k + "=" + v;
    if (out.size() + part.size() + (out.empty() ? 0 : 1) > PAYLOAD_MAX) return;  // nie mieści się: pomiń
    if (!out.empty()) out += ';';
    out += part;
  };
#ifdef USE_SENSOR
  for (auto &it : this->sensors_) {
    if (!it.s->has_state() || std::isnan(it.s->state)) continue;
    char buf[24];
    snprintf(buf, sizeof(buf), "%.*f", it.decimals, it.s->state);
    add(it.key, buf);
  }
#endif
#ifdef USE_BINARY_SENSOR
  for (auto &it : this->binaries_)
    if (it.s->has_state()) add(it.key, it.s->state ? "1" : "0");
#endif
  return out;
}

bool SensmosLora::budget_ok_(uint32_t airtime_ms) {
  const uint32_t now = millis();
  while (!this->sent_.empty() && now - this->sent_.front().first > HOUR_MS) this->sent_.pop_front();
  uint32_t used = 0;
  for (auto &s : this->sent_) used += s.second;
  return used + airtime_ms <= this->budget_ms_;
}

bool SensmosLora::send_now(const std::string &extra) {
  if (this->is_failed() || this->radio_ == nullptr) return false;
  std::string text = this->build_payload();
  if (!extra.empty()) {
    if (!text.empty() && text.size() + 1 + extra.size() <= PAYLOAD_MAX) text += ';' + extra;
    else if (text.empty() && extra.size() <= PAYLOAD_MAX) text = extra;
  }
  if (text.empty()) {
    ESP_LOGD(TAG, "brak odczytów, nic do nadania");
    return false;
  }
  if (!frame_text_ok(reinterpret_cast<const uint8_t *>(text.data()), text.size())) {
    ESP_LOGW(TAG, "treść ma niedozwolone znaki (tylko ASCII bez \" i \\): %s", text.c_str());
    this->sent_err_++;
    return false;
  }
  uint8_t frame[FRAME_MAX];
  uint8_t nonce[FRAME_NONCE];
  const bool aes = this->key_ok_;
  if (aes) {
    const uint32_t r = random_uint32();
    memcpy(nonce, &r, FRAME_NONCE);
  }
  const size_t n = frame_build(frame, this->dst_, this->sub_, reinterpret_cast<const uint8_t *>(text.data()),
                               text.size(), aes ? this->key_ : nullptr, nonce, ctr_mbedtls);
  if (n == 0) {
    this->sent_err_++;
    return false;
  }
  const uint32_t air = lora_airtime_ms(n, this->sf_, this->bw_hz_, this->cr_, this->preamble_);
  if (!this->budget_ok_(air)) {
    ESP_LOGW(TAG, "limit czasu nadawania (%u ms/h) wyczerpany, ramka %u ms czeka na kolejną okazję",
             (unsigned) this->budget_ms_, (unsigned) air);
    this->sent_skip_++;
    return false;
  }
  const auto err = this->radio_->transmit_packet(frame, n);
  if (static_cast<int>(err) != 0) {  // NONE = 0 (w nowszym ESPHome: SX126X_ERROR_NONE)
    ESP_LOGW(TAG, "nadanie nie wyszło (błąd radia %d)", (int) err);
    this->sent_err_++;
    return false;
  }
  this->sent_.emplace_back(millis(), air);
  this->sent_ok_++;
  ESP_LOGI(TAG, "nadano do %02x%02x%02x%02x sub %u (%u B, %u ms, %s): %s", this->dst_[0], this->dst_[1],
           this->dst_[2], this->dst_[3], this->sub_, (unsigned) n, (unsigned) air, aes ? "AES" : "jawnie",
           text.c_str());
  return true;
}

void SensmosLora::update() { this->send_now(); }

void SensmosLora::loop() {
  if (!this->pending_change_) return;
  const uint32_t now = millis();
  if (this->last_change_tx_ != 0 && now - this->last_change_tx_ < CHANGE_GAP_MS) return;
  this->pending_change_ = false;
  this->last_change_tx_ = now;
  this->send_now();
}

void SensmosLora::dump_config() {
  ESP_LOGCONFIG(TAG, "Sensmos LoRa:");
  ESP_LOGCONFIG(TAG, "  Adresat (hub): %02x%02x%02x%02x, sub: %u", this->dst_[0], this->dst_[1], this->dst_[2],
                this->dst_[3], this->sub_);
  ESP_LOGCONFIG(TAG, "  Szyfrowanie: %s", this->key_ok_ ? "AES-256 (klucz z frazy)" : "brak (open: true)");
  ESP_LOGCONFIG(TAG, "  Radio: SF%u, %u kHz, CR 4/%u; limit nadawania %u ms/h", this->sf_,
                (unsigned) (this->bw_hz_ / 1000), this->cr_, (unsigned) this->budget_ms_);
  LOG_UPDATE_INTERVAL(this);
}

}  // namespace sensmos_lora
}  // namespace esphome
