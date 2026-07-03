// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "../../src/flpsi/flpsi_wrapper.h"
#include <iostream>
#include <random>
#include <cassert>
#include <cmath>
#include <fstream>
#include <string>

using namespace flpsi;

// NDEBUG-independent check (assert() compiles out under -DNDEBUG). Used by the
// loadDataset tests, which guard the integrity of headline experiment numbers.
static int g_fail = 0;
#define REQUIRE(cond, msg) do { if (!(cond)) { \
  std::cerr << "CHECK FAILED: " << (msg) << " @ " << __FILE__ << ":" << __LINE__ << "\n"; \
  g_fail = 1; } } while (0)

int main() {
  std::cout << "\n=== FLPSI Wrapper Test ===\n\n";

  // Initialize hyperplanes (L x 128, each row L2-normalized)
  std::mt19937 rng(42);
  std::normal_distribution<float> normal(0.0f, 1.0f);

  std::vector<std::vector<float>> hyperplanes(L, std::vector<float>(128));
  for (int i = 0; i < L; ++i) {
    float norm = 0.0f;
    for (int j = 0; j < 128; ++j) {
      hyperplanes[i][j] = normal(rng);
      norm += hyperplanes[i][j] * hyperplanes[i][j];
    }
    norm = std::sqrt(norm);
    for (int j = 0; j < 128; ++j) {
      hyperplanes[i][j] /= norm;
    }
  }

  std::cout << "Hyperplanes initialized: " << L << " x 128, L2-normalized\n\n";

  // Create 2 random embeddings
  std::vector<float> emb1(128), emb2(128);
  for (int i = 0; i < 128; ++i) {
    emb1[i] = normal(rng);
    emb2[i] = normal(rng);
  }

  // Encode embeddings to bitvectors
  auto bv1 = lshEncode(emb1, hyperplanes);
  auto bv2 = lshEncode(emb2, hyperplanes);

  std::cout << "Encoded 2 embeddings to L=" << L << "-bit bitvectors\n\n";

  // Create subsample masks
  auto masks = makeSubsampleMasks(rng, 123);
  std::cout << "Created " << N_SUB << " subsample masks\n\n";

  // Subsample both vectors
  auto sub1 = subsample(bv1, masks);
  auto sub2 = subsample(bv2, masks);

  std::cout << "Subsampled both bitvectors: " << N_SUB << " field elements each\n\n";

  // Count agreements
  int agreements = 0;
  for (size_t i = 0; i < sub1.size(); ++i) {
    if (sub1[i] == sub2[i]) {
      agreements++;
    }
  }
  std::cout << "Agreements: " << agreements << " / " << N_SUB << "\n";
  std::cout << "  (same random embeddings usually disagree)\n\n";

  // Test 1: identical vectors should give 100% agreement
  std::cout << "Test 1: Identical vectors\n";
  auto sub1_again = subsample(bv1, masks);
  int agreements_identical = 0;
  for (size_t i = 0; i < sub1.size(); ++i) {
    if (sub1[i] == sub1_again[i]) {
      agreements_identical++;
    }
  }
  std::cout << "  Agreements: " << agreements_identical << " / " << N_SUB << "\n";
  assert(agreements_identical == N_SUB && "Identical vectors should match 100%");
  std::cout << "  PASSED\n\n";

  // Test 2: flip 10 bits and check agreement drops
  std::cout << "Test 2: Flip 10 bits\n";
  auto bv1_flipped = bv1;
  for (int i = 0; i < 10; ++i) {
    int bit_idx = i * 25; // Spread across bits
    bv1_flipped[bit_idx / 8] ^= (1 << (bit_idx % 8));
  }

  auto sub1_flipped = subsample(bv1_flipped, masks);
  int agreements_flipped = 0;
  for (size_t i = 0; i < sub1.size(); ++i) {
    if (sub1[i] == sub1_flipped[i]) {
      agreements_flipped++;
    }
  }
  std::cout << "  Agreements after 10-bit flip: " << agreements_flipped << " / " << N_SUB << "\n";
  assert(agreements_flipped < N_SUB && "Flipped bits should reduce agreement");
  std::cout << "  PASSED\n\n";

  // Test 3: robust template with 2 identical samples (should be identical)
  std::cout << "Test 3: Robust template (2 identical samples)\n";
  auto robust = robustTemplate({bv1, bv1});
  assert(robust == bv1 && "Robust template of identical samples should be identical");
  std::cout << "  PASSED\n\n";

  // ---------------------------------------------------------------------------
  // Test 4: loadDataset round-trips FPB1 (BITS + EMBED) and legacy LFWE.
  // Values chosen to be exactly representable in float, so == is safe.
  // ---------------------------------------------------------------------------
  std::cout << "Test 4: loadDataset (FPB1 BITS/EMBED + legacy LFWE)\n";
  {
    auto wr_i32 = [](std::ofstream& o, int32_t v){ o.write((char*)&v, 4); };
    auto wr_i64 = [](std::ofstream& o, int64_t v){ o.write((char*)&v, 8); };

    // FPB1/BITS: n=3, L=16 (2 code bytes), role_present=1
    const std::string pbits = "/tmp/test_fpb_bits.bin";
    int32_t bids[3]      = {7, 42, 1000};
    uint8_t broles[3]    = {0, 0, 1};
    uint8_t bcodes[3][2] = {{0xAB,0xCD},{0x01,0x80},{0xFF,0x00}};
    {
      std::ofstream o(pbits, std::ios::binary);
      o.write("FPB1", 4); wr_i32(o,1); wr_i32(o,1); wr_i64(o,3); wr_i32(o,16); wr_i32(o,1);
      for (int i=0;i<3;++i){ wr_i32(o,bids[i]); o.write((char*)&broles[i],1); o.write((char*)bcodes[i],2); }
    }
    Dataset db = loadDataset(pbits);
    REQUIRE(db.mode == DataMode::BITS, "BITS mode");
    REQUIRE(db.dim_or_L == 16, "BITS L");
    REQUIRE(db.role_present, "BITS role_present");
    REQUIRE(db.records.size() == 3, "BITS record count");
    for (int i=0;i<3;++i){
      REQUIRE(db.records[i].id == bids[i], "BITS id");
      REQUIRE(db.records[i].role == broles[i], "BITS role");
      REQUIRE(db.records[i].code.size() == 2, "BITS code size");
      REQUIRE(db.records[i].code[0]==bcodes[i][0] && db.records[i].code[1]==bcodes[i][1], "BITS code bytes");
    }
    std::cout << "  FPB1/BITS round-trip OK\n";

    // FPB1/EMBED: n=2, dim=4, role_present=0
    const std::string pemb = "/tmp/test_fpb_embed.bin";
    int32_t eids[2]   = {3, 9};
    float   eemb[2][4]= {{0.5f,-1.0f,2.0f,0.0f},{1.5f,3.25f,-0.75f,4.0f}};
    {
      std::ofstream o(pemb, std::ios::binary);
      o.write("FPB1", 4); wr_i32(o,1); wr_i32(o,0); wr_i64(o,2); wr_i32(o,4); wr_i32(o,0);
      for (int i=0;i<2;++i){ wr_i32(o,eids[i]); o.write((char*)eemb[i], 4*sizeof(float)); }
    }
    Dataset de = loadDataset(pemb);
    REQUIRE(de.mode == DataMode::EMBED, "EMBED mode");
    REQUIRE(de.dim_or_L == 4, "EMBED dim");
    REQUIRE(!de.role_present, "EMBED role_present");
    REQUIRE(de.records.size() == 2, "EMBED record count");
    for (int i=0;i<2;++i){
      REQUIRE(de.records[i].id == eids[i], "EMBED id");
      REQUIRE(de.records[i].emb.size() == 4, "EMBED emb size");
      for (int j=0;j<4;++j) REQUIRE(de.records[i].emb[j] == eemb[i][j], "EMBED emb value");
    }
    std::cout << "  FPB1/EMBED round-trip OK\n";

    // Legacy LFWE: little-endian int32 magic 0x4C465745, n=2, dim=3
    const std::string plfwe = "/tmp/test_lfwe.bin";
    int32_t lids[2]   = {11, 22};
    float   lemb[2][3]= {{1.0f,2.0f,3.0f},{-4.0f,-5.0f,-6.0f}};
    {
      std::ofstream o(plfwe, std::ios::binary);
      wr_i32(o, 0x4C465745); wr_i32(o,2); wr_i32(o,3);
      for (int i=0;i<2;++i){ wr_i32(o,lids[i]); o.write((char*)lemb[i], 3*sizeof(float)); }
    }
    Dataset dl = loadDataset(plfwe);
    REQUIRE(dl.mode == DataMode::EMBED, "LFWE mode");
    REQUIRE(dl.dim_or_L == 3, "LFWE dim");
    REQUIRE(!dl.role_present, "LFWE role_present");
    REQUIRE(dl.records.size() == 2, "LFWE record count");
    for (int i=0;i<2;++i){
      REQUIRE(dl.records[i].id == lids[i], "LFWE id");
      for (int j=0;j<3;++j) REQUIRE(dl.records[i].emb[j] == lemb[i][j], "LFWE emb value");
    }
    std::cout << "  LFWE round-trip OK\n";
  }
  std::cout << "  PASSED\n\n";

  // ---------------------------------------------------------------------------
  // Test 5: loadDataset rejects malformed files (must throw, not misparse).
  // ---------------------------------------------------------------------------
  std::cout << "Test 5: loadDataset rejects malformed files\n";
  {
    auto wr_i32 = [](std::ofstream& o, int32_t v){ o.write((char*)&v, 4); };
    auto wr_i64 = [](std::ofstream& o, int64_t v){ o.write((char*)&v, 8); };
    auto expect_throw = [](const std::string& p, const std::string& what){
      bool threw = false;
      try { loadDataset(p); } catch (const std::exception&) { threw = true; }
      REQUIRE(threw, std::string("expected throw: ") + what);
    };

    // Truncated FPB1/BITS: header claims 3 records, only 2 written.
    const std::string ptrunc = "/tmp/test_fpb_trunc.bin";
    {
      std::ofstream o(ptrunc, std::ios::binary);
      o.write("FPB1", 4); wr_i32(o,1); wr_i32(o,1); wr_i64(o,3); wr_i32(o,16); wr_i32(o,1);
      uint8_t code[2] = {0x00,0x00};
      for (int i=0;i<2;++i){ wr_i32(o,i); uint8_t role=0; o.write((char*)&role,1); o.write((char*)code,2); }
    }
    expect_throw(ptrunc, "truncated FPB1");

    // Unknown magic (neither FPB1 nor LFWE).
    const std::string pbadmagic = "/tmp/test_badmagic.bin";
    { std::ofstream o(pbadmagic, std::ios::binary); o.write("FPB2", 4); wr_i32(o,1); }
    expect_throw(pbadmagic, "unknown magic");

    // Bad FPB1 version.
    const std::string pbadver = "/tmp/test_badver.bin";
    { std::ofstream o(pbadver, std::ios::binary);
      o.write("FPB1", 4); wr_i32(o,2); wr_i32(o,1); wr_i64(o,1); wr_i32(o,16); wr_i32(o,0); }
    expect_throw(pbadver, "bad FPB1 version");

    // Bad dim_or_L (zero).
    const std::string pbaddim = "/tmp/test_baddim.bin";
    { std::ofstream o(pbaddim, std::ios::binary);
      o.write("FPB1", 4); wr_i32(o,1); wr_i32(o,0); wr_i64(o,1); wr_i32(o,0); wr_i32(o,0); }
    expect_throw(pbaddim, "zero dim_or_L");

    if (!g_fail) std::cout << "  all malformed inputs rejected\n  PASSED\n\n";
  }

  if (g_fail) { std::cerr << "=== TEST FAILURES ===\n"; return 1; }
  std::cout << "=== All tests passed ===\n\n";
  return 0;
}
