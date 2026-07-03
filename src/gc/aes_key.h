// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#pragma once
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <random>

namespace cstpsi::gc {

using AesKey = std::array<uint8_t, 16>;

inline AesKey generateAesKey() {
    AesKey key;
    std::random_device rd;
    for (int i = 0; i < 4; i++) {
        uint32_t val = rd();
        key[i*4+0] = (val >> 0) & 0xFF;
        key[i*4+1] = (val >> 8) & 0xFF;
        key[i*4+2] = (val >> 16) & 0xFF;
        key[i*4+3] = (val >> 24) & 0xFF;
    }
    return key;
}

inline void saveAesKey(const AesKey& key, const std::string& path) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open key file for writing: " + path);
    f.write(reinterpret_cast<const char*>(key.data()), 16);
}

inline AesKey loadAesKey(const std::string& path) {
    AesKey key;
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open key file for reading: " + path);
    f.read(reinterpret_cast<char*>(key.data()), 16);
    if (f.gcount() != 16) throw std::runtime_error("Key file too short: " + path);
    return key;
}

inline uint64_t reduceAesOutput(const uint8_t output[16], uint64_t field_modulus) {
    __uint128_t val = 0;
    for (int i = 0; i < 16; i++)
        val = (val << 8) | output[i];
    uint64_t result = static_cast<uint64_t>(val % static_cast<__uint128_t>(field_modulus));
    return (result == 0) ? 1 : result;
}

} // namespace cstpsi::gc
