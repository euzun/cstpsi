// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "aes_gc.h"
#include <cstring>
#include <stdexcept>
#include <memory>
#include <openssl/evp.h>

#if __has_include(<emp-sh2pc/emp-sh2pc.h>)
#define EMP_AVAILABLE 1
#include <emp-sh2pc/emp-sh2pc.h>
#include <emp-tool/circuits/circuit_file.h>
#include <thread>
#include <atomic>
#include <chrono>
// Path to AES-non-expanded Bristol Format circuit (installed with emp-tool headers)
#define AES_CIRCUIT_PATH \
    (std::string(EMP_INSTALL_INCLUDE_DIR) + \
     "/emp-tool/circuits/files/bristol_format/AES-non-expanded.txt")

// The Bristol-format AES circuit uses MSB-first bit numbering (bit 0 = MSB of byte 0),
// while emp::Integer loads bytes LSB-first (bit 0 = LSB of byte 0).
// To reconcile: reverse bits within each byte on both circuit inputs and the output.
static void bitReverseBytes(uint8_t* dst, const uint8_t* src, int n) {
    for (int i = 0; i < n; i++) {
        uint8_t b = src[i];
        b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
        b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
        b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
        dst[i] = b;
    }
}
#endif

namespace cstpsi::gc {

// RAII guard for EVP_CIPHER_CTX
struct EVPCtxGuard {
    EVP_CIPHER_CTX* ctx;

    EVPCtxGuard() : ctx(EVP_CIPHER_CTX_new()) {
        if (!ctx) {
            throw std::runtime_error("EVP_CIPHER_CTX_new() failed");
        }
    }

    ~EVPCtxGuard() {
        if (ctx) {
            EVP_CIPHER_CTX_free(ctx);
        }
    }

    // Prevent copying
    EVPCtxGuard(const EVPCtxGuard&) = delete;
    EVPCtxGuard& operator=(const EVPCtxGuard&) = delete;
};

uint64_t blindItemOffline(uint64_t item, const AesKey& key, uint64_t field_modulus) {
    // Zero-pad item to 128-bit block (item in low 8 bytes, little-endian)
    uint8_t block[16] = {};
    memcpy(block, &item, sizeof(uint64_t));

    uint8_t output[16];
    int out_len = 0;

    EVPCtxGuard ctx_guard;
    EVP_CIPHER_CTX* ctx = ctx_guard.ctx;
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key.data(), nullptr);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_EncryptUpdate(ctx, output, &out_len, block, 16);

    return reduceAesOutput(output, field_modulus);
}

void blindDatabaseOffline(
    I2SSM& ss_map,
    const AesKey& key,
    uint64_t field_modulus
) {
    for (auto& [id, subsamples] : ss_map)
        for (auto& item : subsamples)
            item = blindItemOffline(item, key, field_modulus);
}

// Helper: load AES Bristol Format circuit per thread.
// BristolFormat::compute modifies the internal wires[] array, so sharing a single
// instance between threads (as in gcSimulateLocal) would cause a data race.
#ifdef EMP_AVAILABLE
static emp::BristolFormat& getAesCircuit() {
    thread_local emp::BristolFormat cf(AES_CIRCUIT_PATH.c_str());
    return cf;
}

// RAII guard for emp::NetIO: ensures finalize_semi_honest() is called BEFORE destruction
struct EmpNetIOGuard {
    emp::NetIO* io;

    explicit EmpNetIOGuard(emp::NetIO* io_ptr) : io(io_ptr) {}

    ~EmpNetIOGuard() {
        if (io) {
            emp::finalize_semi_honest();
            delete io;
        }
    }

    // Prevent copying
    EmpNetIOGuard(const EmpNetIOGuard&) = delete;
    EmpNetIOGuard& operator=(const EmpNetIOGuard&) = delete;
};
#endif

void gcSenderRole(
    const AesKey& key,
    int emp_port,
    int receiver_item_count,
    uint64_t field_modulus
) {
#ifdef EMP_AVAILABLE
    emp::NetIO* io = new emp::NetIO(nullptr, emp_port);
    EmpNetIOGuard io_guard(io);  // Ensures finalize + delete even on exception
    emp::setup_semi_honest(io, emp::ALICE);

    emp::BristolFormat& cf = getAesCircuit();

    for (int i = 0; i < receiver_item_count; i++) {
        // AES-non-expanded circuit: input1=plaintext(128b), input2=key(128b) → output=cipher(128b)
        // BOB (receiver) provides plaintext; ALICE (sender) provides key.
        // Bristol circuit uses MSB-first bit ordering; reverse bits per byte to match.
        uint8_t key_br[16], dummy_plaintext[16] = {};
        bitReverseBytes(key_br, key.data(), 16);
        emp::Integer plaintext(128, dummy_plaintext, emp::BOB);  // BOB's input — dummy on sender side
        emp::Integer key_int(128, key_br, emp::ALICE);
        emp::Integer cipher(128, (int64_t)0, emp::PUBLIC);
        cf.compute((emp::block*)cipher.bits.data(),
                   (emp::block*)plaintext.bits.data(),
                   (emp::block*)key_int.bits.data());
        // Reveal to BOB only — sender (ALICE) discards the output
        uint8_t discard[16] = {};
        cipher.reveal(discard, emp::BOB);
    }
#else
    (void)key; (void)emp_port; (void)receiver_item_count; (void)field_modulus;
    throw std::runtime_error("EMP libraries not available — run scripts/install_emp.sh first");
#endif
}

std::vector<uint64_t> gcReceiverRole(
    const std::vector<uint64_t>& query_items,
    const std::string& sender_addr,
    int emp_port,
    uint64_t field_modulus
) {
#ifdef EMP_AVAILABLE
    emp::NetIO* io = new emp::NetIO(sender_addr.c_str(), emp_port);
    EmpNetIOGuard io_guard(io);  // Ensures finalize + delete even on exception
    emp::setup_semi_honest(io, emp::BOB);

    emp::BristolFormat& cf = getAesCircuit();

    std::vector<uint64_t> result;
    result.reserve(query_items.size());

    for (uint64_t item : query_items) {
        // Zero-pad item to 128-bit block (item in low 8 bytes, little-endian)
        uint8_t item_bytes[16] = {};
        memcpy(item_bytes, &item, sizeof(uint64_t));

        // AES-non-expanded: input1=plaintext(BOB), input2=key(ALICE).
        // Bristol circuit uses MSB-first bit ordering; reverse bits per byte to match.
        uint8_t item_br[16] = {};
        bitReverseBytes(item_br, item_bytes, 16);
        emp::Integer plaintext(128, item_br, emp::BOB);
        emp::Integer key_int(128, (int64_t)0, emp::ALICE);  // ALICE's input — dummy on receiver side
        emp::Integer cipher(128, (int64_t)0, emp::PUBLIC);
        cf.compute((emp::block*)cipher.bits.data(),
                   (emp::block*)plaintext.bits.data(),
                   (emp::block*)key_int.bits.data());

        // Receiver reveals output, then un-reverse bits per byte to get standard AES output
        uint8_t raw[16] = {}, output[16] = {};
        cipher.reveal(raw, emp::BOB);
        bitReverseBytes(output, raw, 16);
        result.push_back(reduceAesOutput(output, field_modulus));
    }

    return result;
#else
    (void)query_items; (void)sender_addr; (void)emp_port; (void)field_modulus;
    throw std::runtime_error("EMP libraries not available — run scripts/install_emp.sh first");
#endif
}

std::vector<uint64_t> gcSimulateLocal(
    const AesKey& key,
    const std::vector<uint64_t>& query_items,
    uint64_t field_modulus
) {
#ifdef EMP_AVAILABLE
    static std::atomic<int> port_counter{18000};
    std::vector<uint64_t> result;
    std::exception_ptr sender_ex;
    int local_port = port_counter.fetch_add(2);

    std::thread sender_thread([&]() {
        try {
            gcSenderRole(key, local_port, static_cast<int>(query_items.size()), field_modulus);
        } catch (...) { sender_ex = std::current_exception(); }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    try {
        result = gcReceiverRole(query_items, "127.0.0.1", local_port, field_modulus);
    } catch (...) {
        sender_thread.join();
        throw;
    }

    sender_thread.join();
    if (sender_ex) std::rethrow_exception(sender_ex);
    return result;
#else
    // EMP not available: simulate GC locally by calling blindItemOffline directly.
    // Both sender and receiver apply the same AES key to the same items, so the
    // output is identical to what the GC would produce. This path is used in
    // benchmark mode until EMP is installed.
    std::vector<uint64_t> result;
    result.reserve(query_items.size());
    for (uint64_t item : query_items)
        result.push_back(blindItemOffline(item, key, field_modulus));
    return result;
#endif
}

} // namespace cstpsi::gc
