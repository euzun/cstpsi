// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#pragma once
#include "aes_key.h"
#include "helper.h"
#include <cstdint>
#include <string>
#include <vector>

namespace cstpsi::gc {

uint64_t blindItemOffline(uint64_t item, const AesKey& key, uint64_t field_modulus);

void blindDatabaseOffline(
    I2SSM& ss_map,
    const AesKey& key,
    uint64_t field_modulus
);

void gcSenderRole(
    const AesKey& key,
    int emp_port,
    int receiver_item_count,
    uint64_t field_modulus
);

std::vector<uint64_t> gcReceiverRole(
    const std::vector<uint64_t>& query_items,
    const std::string& sender_addr,
    int emp_port,
    uint64_t field_modulus
);

std::vector<uint64_t> gcSimulateLocal(
    const AesKey& key,
    const std::vector<uint64_t>& query_items,
    uint64_t field_modulus
);

} // namespace cstpsi::gc
