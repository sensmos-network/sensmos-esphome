// Test ramki DATA na komputerze: g++ -std=c++17 -I../components frame_test.cpp -lcrypto
// Wypisuje ramki hex dla wektorów; test/frame.test.js dekoduje je kodem Huba (src/lora.js).
#include "sensmos_lora/frame.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstdio>
#include <string>

using namespace esphome::sensmos_lora;

static void ctr_openssl(const uint8_t key[32], const uint8_t iv[16], uint8_t *buf, size_t n) {
  EVP_CIPHER_CTX *c = EVP_CIPHER_CTX_new();
  int out = 0;
  EVP_EncryptInit_ex(c, EVP_aes_256_ctr(), nullptr, key, iv);
  EVP_EncryptUpdate(c, buf, &out, buf, (int) n);
  EVP_CIPHER_CTX_free(c);
}

static void hex(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) printf("%02x", d[i]);
}

int main(int argc, char **argv) {
  // argv: id8 sub fraza|- tekst nonce8hex
  if (argc != 6) return 2;
  uint8_t dst[4], nonce[4];
  for (int i = 0; i < 4; i++) sscanf(argv[1] + 2 * i, "%2hhx", &dst[i]);
  for (int i = 0; i < 4; i++) sscanf(argv[5] + 2 * i, "%2hhx", &nonce[i]);
  std::string phrase = argv[3], text = argv[4];
  uint8_t key[32];
  const bool aes = phrase != "-";
  if (aes) SHA256(reinterpret_cast<const uint8_t *>(phrase.data()), phrase.size(), key);
  uint8_t f[FRAME_MAX];
  size_t n = frame_build(f, dst, (uint8_t) atoi(argv[2]), reinterpret_cast<const uint8_t *>(text.data()), text.size(),
                         aes ? key : nullptr, nonce, ctr_openssl);
  if (!n) return 1;
  hex(f, n);
  printf(" %u\n", lora_airtime_ms(n, 11, 125000, 5, 8));
  return 0;
}
