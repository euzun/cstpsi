// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef FLPSI_WRAPPER_H
#define FLPSI_WRAPPER_H

#include <vector>
#include <cstdint>
#include <random>
#include <string>

namespace flpsi {

// Constants
constexpr int L = 256;                   // Bio-bit length (LSH hyperplanes)
constexpr int N_SUB = 64;                // Subsample count
constexpr int K_MATCH = 2;               // Agreement threshold for k-of-N
constexpr int SUBSAMPLE_BITS = 14;       // Bits per position in subsample
constexpr uint64_t F = 8519681ULL;       // FIELD_MODULUS
constexpr double DEFAULT_BIT_RATIO = 0.9; // Robust template voting threshold

// Type: subsample masks (pair of disjoint 7-element index sets for each of N_SUB)
using SubsampleMasks = std::vector<std::pair<std::vector<int>, std::vector<int>>>;

/**
 * Localization-Sensitive Hashing (LSH) encoding of a face embedding.
 *
 * Computes an L-bit bitvector via dot products with L hyperplanes.
 * Each bit is determined by the sign of the dot product with the corresponding hyperplane.
 *
 * @param embedding       Input embedding (dim=128)
 * @param hyperplanes     L x 128 matrix of hyperplanes, L2-normalized row-wise
 * @return                L-bit bitvector as vector<uint8_t> with 8 bits per byte
 */
std::vector<uint8_t> lshEncode(const std::vector<float>& embedding,
                                const std::vector<std::vector<float>>& hyperplanes);

/**
 * Compute a robust template via majority voting over multiple bitvectors.
 *
 * For each bit position, output 1 if >= (bit_ratio * vecs.size()) votes; else 0.
 *
 * @param bit_vecs    List of L-bit vectors (e.g., from multiple enrollment samples)
 * @param bit_ratio   Voting threshold (default 0.9)
 * @return            Single L-bit robust template
 */
std::vector<uint8_t> robustTemplate(const std::vector<std::vector<uint8_t>>& bit_vecs,
                                     double bit_ratio = DEFAULT_BIT_RATIO);

/**
 * Generate N_SUB disjoint subsample masks for splitting L bits.
 *
 * Each mask partitions N_SUB pairs of 7-element index sets over [0, L).
 * The entire set of SUBSAMPLE_BITS = 14 bits is covered.
 *
 * @param rng   Seeded MT19937 RNG
 * @param seed  Seed for reproducibility (used to seed rng if needed)
 * @return      N_SUB masks, each with (pos1_indices, pos2_indices)
 */
SubsampleMasks makeSubsampleMasks(std::mt19937& rng, int seed);

/**
 * Subsample a robust bitvector using the provided masks.
 *
 * For each of N_SUB masks:
 *   - Extract bits at pos1_indices, compute as 7-bit value with powers of 2
 *   - Extract bits at pos2_indices, compute as 7-bit value with powers of 2
 *   - Combine: ((pos1_val & 0x3FFF) << SUBSAMPLE_BITS) | (pos2_val & 0x3FFF)
 *   - Reduce modulo F
 *
 * @param robust_vec  L-bit robust template (typically from robustTemplate)
 * @param masks       Subsample masks from makeSubsampleMasks
 * @return            N_SUB field elements in [0, F)
 */
std::vector<uint64_t> subsample(const std::vector<uint8_t>& robust_vec,
                                 const SubsampleMasks& masks);

// ---------------------------------------------------------------------------
// Unified dataset container (FPB1) + legacy LFWE reader.
//
// One loader serves both ingest paths:
//   EMBED: float32 embeddings (LFW)        -> records carry .emb (dim floats)
//   BITS : pre-packed L-bit codes (Deep1B) -> records carry .code (L/8 bytes)
// The FPB1 on-disk layout is documented in experiments/fpb.py. The legacy LFWE
// binary (little-endian int32 magic 0x4C465745) is read as EMBED, no roles.
// ---------------------------------------------------------------------------
enum class DataMode { EMBED = 0, BITS = 1 };

struct DatasetRecord {
  int32_t id = 0;             // label / ground-truth id
  uint8_t role = 0;           // 0=enroll, 1=query (meaningful iff role_present)
  std::vector<float> emb;     // EMBED only (dim floats)
  std::vector<uint8_t> code;  // BITS only (ceil(L/8) bytes, LSB-first packing)
};

struct Dataset {
  DataMode mode = DataMode::EMBED;
  int dim_or_L = 0;           // EMBED: embedding dim; BITS: code length L (bits)
  bool role_present = false;
  std::vector<DatasetRecord> records;
};

/**
 * Load an FPB1 (EMBED or BITS) or legacy LFWE file into a unified Dataset.
 * Fields are read element-by-element into aligned storage (no packed-struct
 * reinterpret), so the unaligned role byte is handled safely. Assumes a
 * little-endian host (all supported targets). Throws std::runtime_error on a
 * missing/short/malformed file.
 */
Dataset loadDataset(const std::string& path);

} // namespace flpsi

#endif // FLPSI_WRAPPER_H
