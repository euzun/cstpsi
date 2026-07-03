// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "flpsi_wrapper.h"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cassert>
#include <iostream>
#include <iomanip>
#include <set>
#include <fstream>
#include <cstring>
#include <stdexcept>

namespace flpsi {

std::vector<uint8_t> lshEncode(const std::vector<float>& embedding,
                                const std::vector<std::vector<float>>& hyperplanes) {
  assert(embedding.size() == 128);
  assert(hyperplanes.size() == L);
  for (const auto& h : hyperplanes) {
    assert(h.size() == 128);
  }

  std::vector<uint8_t> bitvec((L + 7) / 8, 0);

  for (int i = 0; i < L; ++i) {
    float dot = 0.0f;
    for (int j = 0; j < 128; ++j) {
      dot += embedding[j] * hyperplanes[i][j];
    }
    int bit = (dot >= 0.0f) ? 1 : 0;  // sign LSH, matches justitia generateLSH (>=0)
    bitvec[i / 8] |= (bit << (i % 8));
  }

  return bitvec;
}

std::vector<uint8_t> robustTemplate(const std::vector<std::vector<uint8_t>>& bit_vecs,
                                     double bit_ratio) {
  if (bit_vecs.empty()) {
    return std::vector<uint8_t>((L + 7) / 8, 0);
  }

  std::vector<uint8_t> robust((L + 7) / 8, 0);
  int threshold = static_cast<int>(std::ceil(bit_ratio * bit_vecs.size()));

  for (int bit_idx = 0; bit_idx < L; ++bit_idx) {
    int votes = 0;
    for (const auto& vec : bit_vecs) {
      votes += (vec[bit_idx / 8] >> (bit_idx % 8)) & 1;
    }
    if (votes >= threshold) {
      robust[bit_idx / 8] |= (1 << (bit_idx % 8));
    }
  }

  return robust;
}

// FLPSI subsampling masks (matches FLPSI'21 + the author's spec):
// Split the L=256-bit robust vector into two 128-bit halves H1 (bits [0,128))
// and H2 (bits [128,256)). For each of N_SUB subsamples pick SUBSAMPLE_BITS/2 (=7)
// distinct LOCAL positions in [0,128) for H1 (pos1) and 7 for H2 (pos2), with the
// two LOCAL index sets DISJOINT (so the XOR-fold of the two halves keeps 14 bits at
// distinct positions). Masks are public and shared across all records in a repeat.
SubsampleMasks makeSubsampleMasks(std::mt19937& rng, int seed) {
  (void)seed; // RNG already seeded by the caller
  SubsampleMasks masks;
  const int HALF = L / 2;                 // 128
  const int PER = SUBSAMPLE_BITS / 2;     // 7
  std::uniform_int_distribution<int> dist(0, HALF - 1);

  for (int i = 0; i < N_SUB; ++i) {
    std::set<int> used;                    // disjointness is over LOCAL [0,128) indices
    std::vector<int> pos1, pos2;
    while ((int)pos1.size() < PER) {
      int b = dist(rng);
      if (used.insert(b).second) pos1.push_back(b);   // H1 local index
    }
    while ((int)pos2.size() < PER) {
      int b = dist(rng);
      if (used.insert(b).second) pos2.push_back(b);   // H2 local index, disjoint from pos1
    }
    masks.push_back({pos1, pos2});
  }
  return masks;
}

// subsample_j = ( sum_{p in pos1, H1[p]==1} 2^p + sum_{q in pos2, H2[q]==1} 2^q ) mod F
// This is the 128-bit XOR-fold of the two masked halves (pos1,pos2 disjoint -> bits at
// distinct positions), reduced mod F. NOT a packed 14-bit value: the bits sit at their
// real positions in [0,128), so mod F induces the intended aliasing across the 2^14 patterns.
std::vector<uint64_t> subsample(const std::vector<uint8_t>& robust_vec,
                                 const SubsampleMasks& masks) {
  static const std::vector<uint64_t> pow2modF = []() {
    std::vector<uint64_t> v(L / 2);
    uint64_t cur = 1 % F;
    for (int p = 0; p < L / 2; ++p) { v[p] = cur; cur = (cur * 2ULL) % F; }
    return v;
  }();
  const int HALF = L / 2;                 // 128

  std::vector<uint64_t> subsamples;
  subsamples.reserve(N_SUB);
  for (const auto& mask : masks) {
    uint64_t val = 0;
    for (int p : mask.first) {            // H1: read bit at absolute position p in [0,128)
      int bit = (robust_vec[p / 8] >> (p % 8)) & 1;
      if (bit) val = (val + pow2modF[p]) % F;
    }
    for (int q : mask.second) {           // H2: read bit at absolute position HALF+q
      int ab = HALF + q;
      int bit = (robust_vec[ab / 8] >> (ab % 8)) & 1;
      if (bit) val = (val + pow2modF[q]) % F;   // contributes 2^(local q), same fold space
    }
    subsamples.push_back(val);
  }
  return subsamples;
}

// ---------------------------------------------------------------------------
// Dataset loading (FPB1 EMBED/BITS + legacy LFWE)
// ---------------------------------------------------------------------------
namespace {
template <typename T>
T readPod(std::istream& f, const char* what) {
  T v;
  f.read(reinterpret_cast<char*>(&v), sizeof(T));
  if (!f) throw std::runtime_error(std::string("loadDataset: unexpected EOF reading ") + what);
  return v;
}
// Bytes from the current position to end of stream (restores position).
int64_t remainingBytes(std::istream& f) {
  auto cur = f.tellg();
  f.seekg(0, std::ios::end);
  auto end = f.tellg();
  f.seekg(cur);
  return static_cast<int64_t>(end - cur);
}
constexpr int32_t kMaxDimOrL = 1 << 20;  // generous sanity cap (embedding dim or L bits)
}  // namespace

Dataset loadDataset(const std::string& path) {
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
                "loadDataset assumes a little-endian host");
#endif
  std::ifstream f(path, std::ios::binary);
  if (!f) throw std::runtime_error("loadDataset: cannot open " + path);

  char magic[4];
  f.read(magic, 4);
  if (!f) throw std::runtime_error("loadDataset: short header in " + path);

  Dataset ds;

  if (std::memcmp(magic, "FPB1", 4) == 0) {
    int32_t version      = readPod<int32_t>(f, "version");
    if (version != 1) throw std::runtime_error("loadDataset: unsupported FPB1 version");
    int32_t mode         = readPod<int32_t>(f, "mode");
    int64_t n            = readPod<int64_t>(f, "n_records");
    int32_t dim_or_L     = readPod<int32_t>(f, "dim_or_L");
    int32_t role_present = readPod<int32_t>(f, "role_present");
    if (mode != 0 && mode != 1) throw std::runtime_error("loadDataset: bad FPB1 mode");
    if (n <= 0 || dim_or_L <= 0 || dim_or_L > kMaxDimOrL)
      throw std::runtime_error("loadDataset: bad FPB1 header (n/dim_or_L out of range)");
    ds.mode = (mode == 0) ? DataMode::EMBED : DataMode::BITS;
    ds.dim_or_L = dim_or_L;
    ds.role_present = (role_present != 0);
    const int32_t code_bytes = (dim_or_L + 7) / 8;
    const int64_t payload_bytes = (ds.mode == DataMode::EMBED)
        ? static_cast<int64_t>(static_cast<size_t>(dim_or_L) * sizeof(float))
        : code_bytes;
    const int64_t stride = 4 + (ds.role_present ? 1 : 0) + payload_bytes;
    if (n > remainingBytes(f) / stride)
      throw std::runtime_error("loadDataset: FPB1 record count exceeds file size");
    ds.records.resize(static_cast<size_t>(n));
    for (int64_t i = 0; i < n; ++i) {
      DatasetRecord& r = ds.records[static_cast<size_t>(i)];
      r.id = readPod<int32_t>(f, "id");
      if (ds.role_present) r.role = readPod<uint8_t>(f, "role");
      if (ds.mode == DataMode::EMBED) {
        r.emb.resize(static_cast<size_t>(dim_or_L));
        f.read(reinterpret_cast<char*>(r.emb.data()), static_cast<std::streamsize>(payload_bytes));
      } else {
        r.code.resize(static_cast<size_t>(code_bytes));
        f.read(reinterpret_cast<char*>(r.code.data()), code_bytes);
      }
      if (!f) throw std::runtime_error("loadDataset: truncated FPB1 records");
    }
    return ds;
  }

  // Legacy LFWE: the first 4 bytes are the little-endian int32 magic 0x4C465745.
  int32_t lfwe_magic;
  std::memcpy(&lfwe_magic, magic, 4);
  if (lfwe_magic != 0x4C465745)
    throw std::runtime_error("loadDataset: unknown magic in " + path);
  int32_t n   = readPod<int32_t>(f, "n");
  int32_t dim = readPod<int32_t>(f, "dim");
  if (n <= 0 || dim <= 0 || dim > kMaxDimOrL)
    throw std::runtime_error("loadDataset: bad LFWE header (n/dim out of range)");
  ds.mode = DataMode::EMBED;
  ds.dim_or_L = dim;
  ds.role_present = false;
  const int64_t emb_bytes = static_cast<int64_t>(static_cast<size_t>(dim) * sizeof(float));
  const int64_t stride = 4 + emb_bytes;
  if (n > remainingBytes(f) / stride)
    throw std::runtime_error("loadDataset: LFWE record count exceeds file size");
  ds.records.resize(static_cast<size_t>(n));
  for (int32_t i = 0; i < n; ++i) {
    DatasetRecord& r = ds.records[static_cast<size_t>(i)];
    r.id = readPod<int32_t>(f, "label");
    r.emb.resize(static_cast<size_t>(dim));
    f.read(reinterpret_cast<char*>(r.emb.data()), static_cast<std::streamsize>(emb_bytes));
    if (!f) throw std::runtime_error("loadDataset: truncated LFWE records");
  }
  return ds;
}

} // namespace flpsi
