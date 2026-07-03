// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * test_network_framing.cc — Validates SEAL ciphertext serialization round-tripping
 *
 * Tests the serialization/deserialization pipeline via public APIs. The
 * length-prefix helpers readUint32LE/writeUint32LE are private to
 * SealSerializer, so they are exercised indirectly here through the vector /
 * CVector2D round-trips (count and dimension prefixes).
 */

#include "serialization.h"
#include "seal/seal.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace cstpsi;
using namespace seal;

// Globals for tracking test results
static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) {
        printf("  PASS  %s\n", label);
        g_pass++;
    } else {
        printf("  FAIL  %s\n", label);
        g_fail++;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Empty vector serialization/deserialization
// ────────────────────────────────────────────────────────────────────────────

static void test_empty_vector() {
    printf("\n[Test: Empty Ciphertext Vector]\n");

    try {
        std::vector<Ciphertext> empty_vec;
        std::string buffer = SealSerializer::serializeCiphertextVector(empty_vec);

        // Empty vector still has a 4-byte count (0) prefix
        check(buffer.size() == 4, "empty vector serializes to 4 bytes (count only)");
    } catch (const std::exception& e) {
        printf("  Exception: %s\n", e.what());
        check(false, "empty vector serialization");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Test: CVector2D (2D array) empty case
// ────────────────────────────────────────────────────────────────────────────

static void test_empty_cvector2d() {
    printf("\n[Test: Empty CVector2D]\n");

    try {
        CVector2D empty_2d;
        std::string buffer = SealSerializer::serializeCVector2D(empty_2d);

        // Empty 2D has dim1=0, dim2=0 (two uint32s = 8 bytes)
        check(buffer.size() == 8, "empty CVector2D serializes to 8 bytes (dim1, dim2)");
    } catch (const std::exception& e) {
        printf("  Exception: %s\n", e.what());
        check(false, "empty CVector2D serialization");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Truncated buffer deserialization (robustness)
// ────────────────────────────────────────────────────────────────────────────

static void test_truncated_buffer() {
    printf("\n[Test: Truncated Buffer Detection]\n");

    // Create a minimal SEAL context for deserialization
    EncryptionParameters parms(scheme_type::bfv);
    parms.set_poly_modulus_degree(2048);
    parms.set_coeff_modulus(CoeffModulus::BFVDefault(2048));
    parms.set_plain_modulus(1024);
    auto context = std::make_shared<SEALContext>(parms);

    try {
        // Truncated buffer (3 bytes, but deserializer expects at least 4 for count)
        std::string truncated(3, 'x');
        std::vector<Ciphertext> result =
            SealSerializer::deserializeCiphertextVector(truncated, context);

        // Production readUint32LE throws on offset+4 > size, so reaching here
        // (no throw) is itself a failure -- the bounds check would be gone.
        (void)result;
        check(false, "truncated buffer must throw (bounds check missing)");
    } catch (const std::exception& e) {
        // Expected: throws on buffer underflow
        check(true, "truncated buffer throws exception");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Mismatch between count prefix and actual data
// ────────────────────────────────────────────────────────────────────────────

static void test_mismatched_count() {
    printf("\n[Test: Mismatched Count vs Data]\n");

    EncryptionParameters parms(scheme_type::bfv);
    parms.set_poly_modulus_degree(2048);
    parms.set_coeff_modulus(CoeffModulus::BFVDefault(2048));
    parms.set_plain_modulus(1024);
    auto context = std::make_shared<SEALContext>(parms);

    try {
        // Hand-craft a buffer: count=5 but provide no actual ciphertext data
        std::string buffer(4, 0);
        buffer[0] = 5;  // Count = 5 (little-endian: 05 00 00 00)
        // No ciphertext data follows

        std::vector<Ciphertext> result =
            SealSerializer::deserializeCiphertextVector(buffer, context);

        // Should throw on buffer underflow when trying to read count[1]'s size prefix
        check(false, "expected exception on mismatched count");
    } catch (const std::runtime_error& e) {
        // Expected: underflow when trying to read first ciphertext size
        check(true, "mismatched count detected (throws exception)");
    } catch (const std::exception& e) {
        check(true, "mismatched count detected (throws exception)");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Helper to generate a valid minimal SEAL context and Ciphertext
// ────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<seal::SEALContext> create_test_context() {
    seal::EncryptionParameters parms(seal::scheme_type::bfv);
    parms.set_poly_modulus_degree(2048);
    parms.set_coeff_modulus(seal::CoeffModulus::BFVDefault(2048));
    parms.set_plain_modulus(1024);
    return std::make_shared<seal::SEALContext>(parms);
}

static seal::Ciphertext create_dummy_ciphertext(std::shared_ptr<seal::SEALContext> context) {
    seal::KeyGenerator keygen(*context);
    seal::PublicKey public_key;
    keygen.create_public_key(public_key);
    seal::Plaintext plain("0");
    seal::Encryptor encryptor(*context, public_key);
    seal::Ciphertext ctxt;
    encryptor.encrypt(plain, ctxt);
    return ctxt;
}

// ────────────────────────────────────────────────────────────────────────────
// Test: Single ciphertext round-trip
// ────────────────────────────────────────────────────────────────────────────

static void test_single_ciphertext_roundtrip() {
    printf("\n[Test: Single Ciphertext Round-Trip]\n");

    try {
        auto context = create_test_context();
        auto ctxt = create_dummy_ciphertext(context);

        // Serialize a single-element vector
        std::vector<Ciphertext> original{ctxt};
        std::string buffer = SealSerializer::serializeCiphertextVector(original);

        // Deserialize
        std::vector<Ciphertext> deserialized =
            SealSerializer::deserializeCiphertextVector(buffer, context);

        // Check count matches
        check(deserialized.size() == 1, "single ciphertext vector round-trip: count preserved");

        // Check the ciphertext is valid (it should have the same size as original)
        check(deserialized[0].size() == ctxt.size(),
              "single ciphertext vector round-trip: ciphertext size preserved");
    } catch (const std::exception& e) {
        printf("  Exception: %s\n", e.what());
        check(false, "single ciphertext round-trip");
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Main
// ────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== Network Framing Unit Tests (Serialization) ===\n");

    test_empty_vector();
    test_empty_cvector2d();
    test_truncated_buffer();
    test_mismatched_count();
    test_single_ciphertext_roundtrip();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);

    return (g_fail > 0) ? 1 : 0;
}
