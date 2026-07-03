// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "serialization.h"
#include <sstream>
#include <stdexcept>
#include <cstring>

namespace cstpsi {

// Helper: Read little-endian uint32_t
uint32_t SealSerializer::readUint32LE(const std::string& buffer, size_t offset) {
    if (offset + 4 > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading uint32_t");
    }
    uint32_t value = 0;
    const unsigned char* ptr = reinterpret_cast<const unsigned char*>(buffer.data() + offset);
    value = (uint32_t)ptr[0] | ((uint32_t)ptr[1] << 8) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[3] << 24);
    return value;
}

// Helper: Write little-endian uint32_t
void SealSerializer::writeUint32LE(uint32_t value, std::string& buffer) {
    unsigned char bytes[4];
    bytes[0] = static_cast<unsigned char>(value & 0xFF);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
    buffer.append(reinterpret_cast<const char*>(bytes), 4);
}

// Serialize a single Ciphertext
std::string SealSerializer::serializeCiphertext(const seal::Ciphertext& ctxt) {
    std::stringstream ss;
    ctxt.save(ss);
    return ss.str();
}

// Serialize vector<Ciphertext>
std::string SealSerializer::serializeCiphertextVector(
    const std::vector<seal::Ciphertext>& ciphertexts) {
    std::string buffer;

    uint32_t count = static_cast<uint32_t>(ciphertexts.size());
    writeUint32LE(count, buffer);

    for (const auto& ctxt : ciphertexts) {
        std::string ctxt_data = serializeCiphertext(ctxt);
        uint32_t ctxt_size = static_cast<uint32_t>(ctxt_data.size());

        writeUint32LE(ctxt_size, buffer);
        buffer.append(ctxt_data);
    }

    return buffer;
}

// Deserialize a single Ciphertext with offset tracking
seal::Ciphertext SealSerializer::deserializeCiphertext(
    const std::string& buffer,
    size_t& offset,
    std::shared_ptr<seal::SEALContext> context) {
    uint32_t ctxt_size = readUint32LE(buffer, offset);
    offset += 4;

    if (offset + ctxt_size > buffer.size()) {
        throw std::runtime_error("Buffer underflow reading ciphertext data");
    }

    std::string ctxt_data = buffer.substr(offset, ctxt_size);
    offset += ctxt_size;

    std::stringstream ss(ctxt_data);
    seal::Ciphertext ctxt;
    ctxt.load(*context, ss);

    return ctxt;
}

// Deserialize vector<Ciphertext>
std::vector<seal::Ciphertext> SealSerializer::deserializeCiphertextVector(
    const std::string& buffer,
    std::shared_ptr<seal::SEALContext> context) {
    std::vector<seal::Ciphertext> result;
    size_t offset = 0;

    uint32_t count = readUint32LE(buffer, offset);
    offset += 4;

    for (uint32_t i = 0; i < count; i++) {
        result.push_back(deserializeCiphertext(buffer, offset, context));
    }

    return result;
}

// Serialize CVector2D
std::string SealSerializer::serializeCVector2D(const CVector2D& cvector) {
    std::string buffer;

    uint32_t dim1 = static_cast<uint32_t>(cvector.size());
    uint32_t dim2 = 0;
    if (dim1 > 0) {
        dim2 = static_cast<uint32_t>(cvector[0].size());
    }

    writeUint32LE(dim1, buffer);
    writeUint32LE(dim2, buffer);

    for (size_t i = 0; i < cvector.size(); i++) {
        for (size_t j = 0; j < cvector[i].size(); j++) {
            std::string ctxt_data = serializeCiphertext(cvector[i][j]);
            uint32_t ctxt_size = static_cast<uint32_t>(ctxt_data.size());

            writeUint32LE(ctxt_size, buffer);
            buffer.append(ctxt_data);
        }
    }

    return buffer;
}

// Deserialize CVector2D
CVector2D SealSerializer::deserializeCVector2D(
    const std::string& buffer,
    std::shared_ptr<seal::SEALContext> context) {
    CVector2D result;
    size_t offset = 0;

    uint32_t dim1 = readUint32LE(buffer, offset);
    offset += 4;
    uint32_t dim2 = readUint32LE(buffer, offset);
    offset += 4;

    result.resize(dim1);
    for (size_t i = 0; i < dim1; i++) {
        result[i].resize(dim2);
    }

    for (uint32_t i = 0; i < dim1; i++) {
        for (uint32_t j = 0; j < dim2; j++) {
            result[i][j] = deserializeCiphertext(buffer, offset, context);
        }
    }

    return result;
}

} // namespace cstpsi
