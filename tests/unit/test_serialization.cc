// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
// SealSerializer round-trip tests against SEAL 4.x and the current public
// API in src/network/serialization.h.
//
// Public surface under test (everything else is private, exercised
// transitively):
//   - serializeCiphertextVector   / deserializeCiphertextVector
//   - serializeCVector2D          / deserializeCVector2D
//
// Both forms are wire-critical: serialization.cc is the only path
// between sender and receiver (see src/network/network.cc). A bit-wise
// regression here would silently corrupt every network-mode result.

#include "serialization.h"

#include <gtest/gtest.h>
#include <seal/seal.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using cstpsi::SealSerializer;
using seal::BatchEncoder;
using seal::Ciphertext;
using seal::CoeffModulus;
using seal::Decryptor;
using seal::Encryptor;
using seal::EncryptionParameters;
using seal::KeyGenerator;
using seal::Plaintext;
using seal::PublicKey;
using seal::SEALContext;
using seal::SecretKey;
using seal::scheme_type;

namespace {

constexpr std::size_t kPolyModulusDegree = 4096;
constexpr std::uint64_t kPlainModulus = 8519681;  // 23-bit CSTPSI field

class SealSerializationTest : public ::testing::Test {
protected:
    std::shared_ptr<SEALContext> context;
    PublicKey public_key;
    SecretKey secret_key;
    // optional<T> lets us defer construction until SetUp() runs and the
    // SEAL context is ready (SEAL's encryptor/decryptor/encoder don't
    // have default constructors in 4.x).
    std::optional<Encryptor> encryptor;
    std::optional<Decryptor> decryptor;
    std::optional<BatchEncoder> encoder;

    void SetUp() override {
        EncryptionParameters params(scheme_type::bfv);
        params.set_poly_modulus_degree(kPolyModulusDegree);
        params.set_coeff_modulus(CoeffModulus::BFVDefault(kPolyModulusDegree));
        params.set_plain_modulus(kPlainModulus);
        context = std::make_shared<SEALContext>(params);

        KeyGenerator keygen(*context);
        keygen.create_public_key(public_key);
        secret_key = keygen.secret_key();

        encryptor.emplace(*context, public_key);
        decryptor.emplace(*context, secret_key);
        encoder.emplace(*context);
    }

    Ciphertext makeCiphertext(std::uint64_t value) {
        std::vector<std::uint64_t> slots(encoder->slot_count(), value);
        Plaintext plain;
        encoder->encode(slots, plain);
        Ciphertext cipher;
        encryptor->encrypt(plain, cipher);
        return cipher;
    }

    std::uint64_t decryptFirstSlot(const Ciphertext& cipher) {
        Plaintext plain;
        decryptor->decrypt(cipher, plain);
        std::vector<std::uint64_t> slots;
        encoder->decode(plain, slots);
        return slots.empty() ? 0 : slots[0];
    }
};

// ---------------------------------------------------------------------------
// Vector round-trips
// ---------------------------------------------------------------------------

TEST_F(SealSerializationTest, CiphertextVectorRoundTrip) {
    const std::vector<std::uint64_t> values = {1, 42, 100, 9999, kPlainModulus - 1};
    std::vector<Ciphertext> original;
    for (auto v : values) original.push_back(makeCiphertext(v));

    std::string buffer = SealSerializer::serializeCiphertextVector(original);
    EXPECT_GT(buffer.size(), 0u);

    auto restored = SealSerializer::deserializeCiphertextVector(buffer, context);
    ASSERT_EQ(original.size(), restored.size());
    for (std::size_t i = 0; i < values.size(); ++i) {
        EXPECT_EQ(values[i], decryptFirstSlot(restored[i])) << "mismatch at index " << i;
    }
}

TEST_F(SealSerializationTest, VectorOfOneRoundTrip) {
    // Production never sends a single bare ciphertext (the public API only
    // exposes the vector form), but a 1-element vector is the natural
    // edge case for "smallest valid payload".
    std::vector<Ciphertext> original{makeCiphertext(123)};

    auto buffer = SealSerializer::serializeCiphertextVector(original);
    auto restored = SealSerializer::deserializeCiphertextVector(buffer, context);

    ASSERT_EQ(1u, restored.size());
    EXPECT_EQ(123u, decryptFirstSlot(restored[0]));
}

TEST_F(SealSerializationTest, EmptyVectorRoundTrip) {
    std::vector<Ciphertext> empty;
    auto buffer = SealSerializer::serializeCiphertextVector(empty);
    auto restored = SealSerializer::deserializeCiphertextVector(buffer, context);
    EXPECT_EQ(0u, restored.size());
}

TEST_F(SealSerializationTest, LargeCiphertextVectorRoundTrip) {
    constexpr std::size_t kCount = 100;
    std::vector<Ciphertext> original;
    original.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) original.push_back(makeCiphertext(i));

    auto buffer = SealSerializer::serializeCiphertextVector(original);
    auto restored = SealSerializer::deserializeCiphertextVector(buffer, context);

    ASSERT_EQ(kCount, restored.size());
    // Spot-check head, middle, tail (full N=100 decrypt is slow).
    EXPECT_EQ(0u, decryptFirstSlot(restored[0]));
    EXPECT_EQ(50u, decryptFirstSlot(restored[50]));
    EXPECT_EQ(99u, decryptFirstSlot(restored[kCount - 1]));
}

// ---------------------------------------------------------------------------
// 2D matrix round-trips (production wire payload for sender->receiver result)
// ---------------------------------------------------------------------------

TEST_F(SealSerializationTest, CVector2DRoundTrip) {
    constexpr std::size_t kRows = 3, kCols = 4;
    CVector2D original(kRows);
    for (std::size_t i = 0; i < kRows; ++i) {
        original[i].resize(kCols);
        for (std::size_t j = 0; j < kCols; ++j) {
            original[i][j] = makeCiphertext(i * kCols + j);
        }
    }

    auto buffer = SealSerializer::serializeCVector2D(original);
    EXPECT_GT(buffer.size(), 0u);

    auto restored = SealSerializer::deserializeCVector2D(buffer, context);
    ASSERT_EQ(kRows, restored.size());
    for (std::size_t i = 0; i < kRows; ++i) {
        ASSERT_EQ(kCols, restored[i].size()) << "row " << i;
        for (std::size_t j = 0; j < kCols; ++j) {
            EXPECT_EQ(i * kCols + j, decryptFirstSlot(restored[i][j]))
                << "cell (" << i << "," << j << ")";
        }
    }
}

TEST_F(SealSerializationTest, CVector2DSingleRow) {
    constexpr std::size_t kCols = 5;
    CVector2D original(1);
    original[0].resize(kCols);
    for (std::size_t j = 0; j < kCols; ++j) original[0][j] = makeCiphertext(j + 100);

    auto buffer = SealSerializer::serializeCVector2D(original);
    auto restored = SealSerializer::deserializeCVector2D(buffer, context);

    ASSERT_EQ(1u, restored.size());
    ASSERT_EQ(kCols, restored[0].size());
    for (std::size_t j = 0; j < kCols; ++j) {
        EXPECT_EQ(j + 100, decryptFirstSlot(restored[0][j]));
    }
}

TEST_F(SealSerializationTest, CVector2DSingleCol) {
    constexpr std::size_t kRows = 5;
    CVector2D original(kRows);
    for (std::size_t i = 0; i < kRows; ++i) {
        original[i].resize(1);
        original[i][0] = makeCiphertext(i + 200);
    }

    auto buffer = SealSerializer::serializeCVector2D(original);
    auto restored = SealSerializer::deserializeCVector2D(buffer, context);

    ASSERT_EQ(kRows, restored.size());
    for (std::size_t i = 0; i < kRows; ++i) {
        ASSERT_EQ(1u, restored[i].size());
        EXPECT_EQ(i + 200, decryptFirstSlot(restored[i][0]));
    }
}

TEST_F(SealSerializationTest, CVector2DLarge) {
    constexpr std::size_t kRows = 10, kCols = 10;
    CVector2D original(kRows);
    for (std::size_t i = 0; i < kRows; ++i) {
        original[i].resize(kCols);
        for (std::size_t j = 0; j < kCols; ++j) {
            original[i][j] = makeCiphertext(i * kCols + j);
        }
    }

    auto buffer = SealSerializer::serializeCVector2D(original);
    auto restored = SealSerializer::deserializeCVector2D(buffer, context);

    ASSERT_EQ(kRows, restored.size());
    for (std::size_t i = 0; i < kRows; ++i) {
        ASSERT_EQ(kCols, restored[i].size());
        for (std::size_t j = 0; j < kCols; ++j) {
            EXPECT_EQ(i * kCols + j, decryptFirstSlot(restored[i][j]));
        }
    }
}

// ---------------------------------------------------------------------------
// Determinism + error paths
// ---------------------------------------------------------------------------

TEST_F(SealSerializationTest, SerializationIsDeterministic) {
    // Same Ciphertext object MUST serialize to identical bytes both
    // times. Anything else makes diff-based wire-format checks
    // (in-process vs network bit-for-bit equivalence) impossible.
    auto cipher = makeCiphertext(1);
    std::vector<Ciphertext> v{cipher};
    auto a = SealSerializer::serializeCiphertextVector(v);
    auto b = SealSerializer::serializeCiphertextVector(v);
    EXPECT_EQ(a, b);
}

TEST_F(SealSerializationTest, TruncatedBufferThrows) {
    // Header says "1 ciphertext, payload length = max-uint32" but the
    // buffer has no payload bytes. Deserializer must reject.
    const std::string truncated{'\x01', '\x00', '\x00', '\x00',
                                '\xff', '\xff', '\xff', '\xff'};
    EXPECT_THROW(
        SealSerializer::deserializeCiphertextVector(truncated, context),
        std::exception
    );
}

// NOTE: no "corrupted payload throws" test. SEAL's Ciphertext::load()
// does not authenticate the polynomial-coefficient bytes; a mid-payload
// bit flip is accepted at load time and produces garbage at decrypt
// time. Wire-integrity must come from the transport layer (TLS), not
// from the serializer. TruncatedBufferThrows above covers the framing
// error path, which SEAL *does* catch.

}  // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
