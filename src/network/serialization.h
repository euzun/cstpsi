// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_SERIALIZATION
#define CSTPSI_SERIALIZATION

#include "seal/seal.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declaration - CVector2D is defined in params.h
typedef std::vector<std::vector<seal::Ciphertext>> CVector2D;

namespace cstpsi {

/**
 * @class SealSerializer
 * @brief Serializes and deserializes SEAL cryptographic objects for network transmission
 *
 * Provides serialization for:
 * - Single Ciphertext objects
 * - vector<Ciphertext> (1D arrays)
 * - CVector2D (2D arrays of ciphertexts)
 *
 * Uses SEAL's native save()/load() for compact binary format.
 * All serialization is length-prefixed for robust deserialization.
 *
 * Serialization Format:
 * - Single Ciphertext: [size:4B][data:N bytes]
 * - vector<Ciphertext>: [count:4B][ctxt1][ctxt2]...[ctxtN]
 * - CVector2D: [dim1:4B][dim2:4B][ctxt_0_0][ctxt_0_1]...[ctxt_d1_d2]
 *
 * All integers are little-endian.
 */
class SealSerializer {
public:
    /**
     * Serialize a vector of Ciphertext objects
     */
    static std::string serializeCiphertextVector(
        const std::vector<seal::Ciphertext>& ciphertexts
    );

    /**
     * Deserialize a byte buffer to vector<Ciphertext>
     */
    static std::vector<seal::Ciphertext> deserializeCiphertextVector(
        const std::string& buffer,
        std::shared_ptr<seal::SEALContext> context
    );

    /**
     * Serialize a 2D array of Ciphertext objects (CVector2D)
     */
    static std::string serializeCVector2D(
        const CVector2D& cvector
    );

    /**
     * Deserialize a byte buffer to CVector2D
     */
    static CVector2D deserializeCVector2D(
        const std::string& buffer,
        std::shared_ptr<seal::SEALContext> context
    );

private:
    static std::string serializeCiphertext(
        const seal::Ciphertext& ctxt
    );

    static seal::Ciphertext deserializeCiphertext(
        const std::string& buffer,
        size_t& offset,
        std::shared_ptr<seal::SEALContext> context
    );

    static uint32_t readUint32LE(
        const std::string& buffer,
        size_t offset
    );

    static void writeUint32LE(
        uint32_t value,
        std::string& buffer
    );
};

} // namespace cstpsi

#endif // CSTPSI_SERIALIZATION
