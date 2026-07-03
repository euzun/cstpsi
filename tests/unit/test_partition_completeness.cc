// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * test_partition_completeness.cc — Regression test for partition completeness bug
 *
 * Bug fixed: partitionDB was seeding the first partition with a raw shuffled index
 * (a value in [0, actual_enr_count)) instead of the actual enrolled id. This caused:
 *   - One enrolled id to be silently dropped per split (no polynomial built -> no match)
 *   - A spurious numeric id injected (the raw index value)
 *
 * This test enforces the invariant: parallelPartitionDB must place EVERY enrolled id
 * EXACTLY ONCE across all partitions. Critical design: use enrolled ids OUTSIDE the
 * index range [0, count) so that injected raw indices are unambiguously spurious.
 *
 * Test cases:
 *   (a) nrof_splits=1: ~12 subjects (ids 1000..1011), inN=8
 *   (b) nrof_splits=2: ~20 subjects (ids 1000..1019), inN=10
 */

#include "sender.h"
extern "C" {
#include "modpoly.h"
}

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <set>
#include <unordered_map>

using namespace std;

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
// Helper: Flatten partitions into a multiset of placed ids
// ────────────────────────────────────────────────────────────────────────────
static unordered_map<int, int> flattenPartitions(const I32Vector3D& partitions) {
    unordered_map<int, int> placed;  // id -> count
    for (const auto& split : partitions) {
        for (const auto& partition : split) {
            for (int id : partition) {
                placed[id]++;
            }
        }
    }
    return placed;
}

// ────────────────────────────────────────────────────────────────────────────
// Test Case (a): nrof_splits=1, ~12 subjects, inN=8
// ────────────────────────────────────────────────────────────────────────────
static void test_case_a_single_split() {
    printf("\n[Test Case (a): nrof_splits=1, 12 subjects]\n");

    // Save original globals
    int saved_nrof_splits = nrof_splits;
    int saved_inN = inN;
    int saved_partition_size = partition_size;
    int saved_nrof_offline_threads = nrof_offline_threads;

    // Set test globals
    nrof_splits = 1;
    inN = 8;
    partition_size = 4;  // small enough to test multiple partitions per split
    nrof_offline_threads = 1;

    // Initialize polynomial for share generation
    initializeSharePoly(FIELD_MODULUS);

    // Create enrollment map with ids 1000..1011 (outside [0, actual_count) range)
    I2SSM enr_ss_map;
    const int NUM_ENROLLED = 12;
    const int BASE_ID = 1000;
    for (int i = 0; i < NUM_ENROLLED; i++) {
        int id = BASE_ID + i;
        vector<uint64_t> subsamples(inN);
        // Generate distinct subsamples for each subject
        for (int j = 0; j < inN; j++) {
            subsamples[j] = (id * 100 + j) % FIELD_MODULUS;
        }
        enr_ss_map[id] = subsamples;
    }

    printf("  Enrolled %d subjects with ids [%d, %d]\n", NUM_ENROLLED, BASE_ID, BASE_ID + NUM_ENROLLED - 1);

    // Call parallelPartitionDB
    I32Vector3D partitions = parallelPartitionDB(enr_ss_map);

    // Flatten and check completeness
    auto placed = flattenPartitions(partitions);

    // Check (i): total placed count == enr_ss_map.size()
    int total_placed = 0;
    for (const auto& kv : placed) total_placed += kv.second;
    check(total_placed == (int)enr_ss_map.size(),
          "Case (a): total placed == enrolled count");

    // Check (ii): every enrolled id appears exactly once
    bool all_enrolled_exact = true;
    for (int i = 0; i < NUM_ENROLLED; i++) {
        int id = BASE_ID + i;
        if (placed[id] != 1) {
            all_enrolled_exact = false;
            printf("    [ERROR] enrolled id %d appears %d times (expected 1)\n", id, placed[id]);
        }
    }
    check(all_enrolled_exact, "Case (a): every enrolled id appears exactly once");

    // Check (iii): no spurious ids (all placed ids must be in enrolled set)
    bool no_spurious = true;
    for (const auto& kv : placed) {
        int id = kv.first;
        if (id < BASE_ID || id >= BASE_ID + NUM_ENROLLED) {
            no_spurious = false;
            printf("    [ERROR] spurious id %d found (outside enrolled range [%d, %d))\n",
                   id, BASE_ID, BASE_ID + NUM_ENROLLED);
        }
    }
    check(no_spurious, "Case (a): no spurious ids injected");

    // Restore globals
    nrof_splits = saved_nrof_splits;
    inN = saved_inN;
    partition_size = saved_partition_size;
    nrof_offline_threads = saved_nrof_offline_threads;
}

// ────────────────────────────────────────────────────────────────────────────
// Test Case (b): nrof_splits=2, ~20 subjects, inN=10
// ────────────────────────────────────────────────────────────────────────────
static void test_case_b_multi_split() {
    printf("\n[Test Case (b): nrof_splits=2, 20 subjects]\n");

    // Save original globals
    int saved_nrof_splits = nrof_splits;
    int saved_inN = inN;
    int saved_partition_size = partition_size;
    int saved_nrof_offline_threads = nrof_offline_threads;

    // Set test globals
    nrof_splits = 2;
    inN = 10;
    partition_size = 5;
    nrof_offline_threads = 1;  // single-threaded for deterministic testing

    // Initialize polynomial for share generation
    initializeSharePoly(FIELD_MODULUS);

    // Create enrollment map with ids 1000..1019 (outside [0, actual_count) range)
    I2SSM enr_ss_map;
    const int NUM_ENROLLED = 20;
    const int BASE_ID = 1000;
    for (int i = 0; i < NUM_ENROLLED; i++) {
        int id = BASE_ID + i;
        vector<uint64_t> subsamples(inN);
        // Generate distinct subsamples: use id*100 + j to ensure variety
        for (int j = 0; j < inN; j++) {
            subsamples[j] = (id * 100 + j) % FIELD_MODULUS;
        }
        enr_ss_map[id] = subsamples;
    }

    printf("  Enrolled %d subjects with ids [%d, %d]\n", NUM_ENROLLED, BASE_ID, BASE_ID + NUM_ENROLLED - 1);
    printf("  Split into %d splits (increment=%d per split)\n", nrof_splits, NUM_ENROLLED / nrof_splits);

    // Call parallelPartitionDB
    I32Vector3D partitions = parallelPartitionDB(enr_ss_map);

    // Flatten and check completeness
    auto placed = flattenPartitions(partitions);

    // Check (i): total placed count == enr_ss_map.size()
    int total_placed = 0;
    for (const auto& kv : placed) total_placed += kv.second;
    check(total_placed == (int)enr_ss_map.size(),
          "Case (b): total placed == enrolled count");

    // Check (ii): every enrolled id appears exactly once
    bool all_enrolled_exact = true;
    for (int i = 0; i < NUM_ENROLLED; i++) {
        int id = BASE_ID + i;
        if (placed[id] != 1) {
            all_enrolled_exact = false;
            printf("    [ERROR] enrolled id %d appears %d times (expected 1)\n", id, placed[id]);
        }
    }
    check(all_enrolled_exact, "Case (b): every enrolled id appears exactly once");

    // Check (iii): no spurious ids (all placed ids must be in enrolled set)
    bool no_spurious = true;
    for (const auto& kv : placed) {
        int id = kv.first;
        if (id < BASE_ID || id >= BASE_ID + NUM_ENROLLED) {
            no_spurious = false;
            printf("    [ERROR] spurious id %d found (outside enrolled range [%d, %d))\n",
                   id, BASE_ID, BASE_ID + NUM_ENROLLED);
        }
    }
    check(no_spurious, "Case (b): no spurious ids across both splits");

    // Additional check for case (b): verify both splits contributed
    set<int> ids_per_split[2];
    for (int s = 0; s < (int)partitions.size(); s++) {
        for (const auto& partition : partitions[s]) {
            for (int id : partition) {
                ids_per_split[s].insert(id);
            }
        }
    }
    printf("  Split 0: %zu unique ids, Split 1: %zu unique ids\n",
           ids_per_split[0].size(), ids_per_split[1].size());
    check(ids_per_split[0].size() > 0 && ids_per_split[1].size() > 0,
          "Case (b): both splits have enrolled ids");

    // Restore globals
    nrof_splits = saved_nrof_splits;
    inN = saved_inN;
    partition_size = saved_partition_size;
    nrof_offline_threads = saved_nrof_offline_threads;
}

// ────────────────────────────────────────────────────────────────────────────
// Main
// ────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== Partition Completeness Regression Tests ===\n");
    printf("FIELD_MODULUS = %ld\n", FIELD_MODULUS);
    printf("This test verifies that parallelPartitionDB places every enrolled id\n");
    printf("exactly once with no missing or spurious entries.\n");
    printf("(Regression: fix changed partitionDB seed from raw index to actual enrolled id)\n");

    test_case_a_single_split();
    test_case_b_multi_split();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);

    // Return non-zero if any test failed (CTest uses exit code)
    return (g_fail > 0) ? 1 : 0;
}
