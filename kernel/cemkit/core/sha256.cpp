#include "cemkit/core/sha256.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace cemkit::core {

namespace {

// Round constants and initial hash value: FIPS 180-4, sections 4.2.2 and 5.3.3.
constexpr std::array<std::uint32_t, 64> k_round{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

constexpr std::array<std::uint32_t, 8> k_initial{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                                 0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                                 0x1f83d9abU, 0x5be0cd19U};

constexpr std::size_t k_block_bytes = 64;

using Block = std::array<std::uint8_t, k_block_bytes>;

void compress(std::array<std::uint32_t, 8>& state, const Block& block) {
  std::array<std::uint32_t, 64> w{};
  for (std::size_t t = 0; t < 16; ++t) {
    w.at(t) = (std::uint32_t{block.at(4 * t)} << 24U) |
              (std::uint32_t{block.at((4 * t) + 1)} << 16U) |
              (std::uint32_t{block.at((4 * t) + 2)} << 8U) | std::uint32_t{block.at((4 * t) + 3)};
  }
  for (std::size_t t = 16; t < 64; ++t) {
    const std::uint32_t s0 =
        std::rotr(w.at(t - 15), 7) ^ std::rotr(w.at(t - 15), 18) ^ (w.at(t - 15) >> 3U);
    const std::uint32_t s1 =
        std::rotr(w.at(t - 2), 17) ^ std::rotr(w.at(t - 2), 19) ^ (w.at(t - 2) >> 10U);
    w.at(t) = w.at(t - 16) + s0 + w.at(t - 7) + s1;
  }

  auto [a, b, c, d, e, f, g, h] = state;
  for (std::size_t t = 0; t < 64; ++t) {
    const std::uint32_t big_s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
    const std::uint32_t choose = (e & f) ^ (~e & g);
    const std::uint32_t temp1 = h + big_s1 + choose + k_round.at(t) + w.at(t);
    const std::uint32_t big_s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = big_s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  const std::array<std::uint32_t, 8> working{a, b, c, d, e, f, g, h};
  for (std::size_t i = 0; i < state.size(); ++i) {
    state.at(i) += working.at(i);
  }
}

}  // namespace

std::string sha256_hex(std::string_view bytes) {
  std::array<std::uint32_t, 8> state = k_initial;
  Block block{};
  std::size_t used = 0;
  for (const char ch : bytes) {
    block.at(used++) = static_cast<std::uint8_t>(ch);
    if (used == k_block_bytes) {
      compress(state, block);
      used = 0;
    }
  }

  // Padding (FIPS 180-4, 5.1.1): a 1 bit, zeros, then the message length in bits as 64-bit
  // big-endian, so that the padded length is a multiple of 512 bits.
  const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8U;
  block.at(used++) = 0x80U;
  if (used > k_block_bytes - 8) {
    while (used < k_block_bytes) {
      block.at(used++) = 0;
    }
    compress(state, block);
    used = 0;
  }
  while (used < k_block_bytes - 8) {
    block.at(used++) = 0;
  }
  for (std::size_t i = 0; i < 8; ++i) {
    block.at(k_block_bytes - 1 - i) = static_cast<std::uint8_t>(bit_length >> (8U * i));
  }
  compress(state, block);

  constexpr std::string_view k_hex_digits = "0123456789abcdef";
  std::string hex;
  hex.reserve(64);
  for (const std::uint32_t word : state) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      hex += k_hex_digits.at((word >> static_cast<unsigned>(shift)) & 0xfU);
    }
  }
  return hex;
}

}  // namespace cemkit::core
