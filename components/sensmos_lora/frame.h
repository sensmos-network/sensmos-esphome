#pragma once
// Ramka DATA Sensmos (0xE0 0x02), ta sama co w firmware noda (lora_scan.cpp) i w Hubie (src/lora.js):
//   [E0][02][flagi][dst 4][sub] + (AES: [nonce 4]) + [payload][CRC32 LE]   flagi: bit0 AES, bit1 LAST
//   AES-256-CTR, licznik = nonce4 | dst4 | sub | 0x00*7, szyfrowany payload+CRC; CRC32 (IEEE) z jawnego payloadu.
// Bez zależności od ESPHome, żeby dało się to sprawdzić testem na komputerze.
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace esphome {
namespace sensmos_lora {

static constexpr uint8_t FRAME_MAGIC = 0xE0;
static constexpr uint8_t FRAME_TYPE_DATA = 0x02;
static constexpr uint8_t FLAG_AES = 0x01;
static constexpr size_t FRAME_HDR = 8;
static constexpr size_t FRAME_NONCE = 4;
static constexpr size_t PAYLOAD_MAX = 128;
static constexpr size_t FRAME_MAX = FRAME_HDR + FRAME_NONCE + PAYLOAD_MAX + 4;

inline uint32_t frame_crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
  }
  return c ^ 0xFFFFFFFFu;
}

// Szyfrowanie AES-256-CTR w miejscu (iv = 16 B licznika). Na płytce: mbedtls, w teście: OpenSSL.
using CtrFn = void (*)(const uint8_t key[32], const uint8_t iv[16], uint8_t *buf, size_t n);

// Treść tekstowa jak w FW (data_is_text): drukowalne ASCII bez " i \.
inline bool frame_text_ok(const uint8_t *p, size_t n) {
  if (n == 0) return false;
  for (size_t i = 0; i < n; i++)
    if (p[i] < 0x20 || p[i] > 0x7E || p[i] == '"' || p[i] == '\\') return false;
  return true;
}

// Składa ramkę do out (min. FRAME_MAX B). key == nullptr = ramka jawna. Zwraca długość albo 0 (zły payload).
inline size_t frame_build(uint8_t *out, const uint8_t dst[4], uint8_t sub, const uint8_t *payload, size_t plen,
                          const uint8_t *key, const uint8_t nonce[4], CtrFn ctr) {
  if (!payload || plen == 0 || plen > PAYLOAD_MAX) return 0;
  if (key && !ctr) return 0;
  out[0] = FRAME_MAGIC;
  out[1] = FRAME_TYPE_DATA;
  out[2] = key ? FLAG_AES : 0;  // LAST (bit1) nigdy: serwer nie przekazuje ramek z LAST
  memcpy(out + 3, dst, 4);
  out[7] = sub;
  size_t p = FRAME_HDR;
  if (key) {
    memcpy(out + p, nonce, FRAME_NONCE);
    p += FRAME_NONCE;
  }
  uint8_t *body = out + p;
  memcpy(body, payload, plen);
  const uint32_t crc = frame_crc32(payload, plen);
  body[plen] = crc & 0xFF;
  body[plen + 1] = (crc >> 8) & 0xFF;
  body[plen + 2] = (crc >> 16) & 0xFF;
  body[plen + 3] = (crc >> 24) & 0xFF;
  if (key) {
    uint8_t iv[16] = {0};
    memcpy(iv, nonce, 4);
    memcpy(iv + 4, dst, 4);
    iv[8] = sub;
    ctr(key, iv, body, plen + 4);
  }
  return p + plen + 4;
}

// Czas nadawania ramki LoRa w ms (wzór Semtech AN1200.13), nagłówek jawny, CRC włączone.
inline uint32_t lora_airtime_ms(size_t len, uint8_t sf, uint32_t bw_hz, uint8_t cr /*5..8*/, uint16_t preamble) {
  const double ts = double(1u << sf) / double(bw_hz) * 1000.0;  // ms na symbol
  const int de = ts > 16.0 ? 1 : 0;                              // low data rate optimize
  const double num = 8.0 * len - 4.0 * sf + 28 + 16;             // CRC on, header on (IH=0)
  double n = num / (4.0 * (sf - 2 * de));
  n = n > 0 ? (n == int(n) ? n : int(n) + 1) : 0;
  const double payload_sym = 8 + n * cr;
  return uint32_t((preamble + 4.25 + payload_sym) * ts + 0.5);
}

}  // namespace sensmos_lora
}  // namespace esphome
