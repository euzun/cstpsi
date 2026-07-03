// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * test_protocol_arithmetic.cc — Core match/reconstruct arithmetic tests
 *
 * Tests the fundamental protocol logic with real Shamir shares but no FHE/network.
 * Scenarios:
 *   (a) TP: matching pair should be found and label reconstructed correctly
 *   (b) TN: non-matching partition should yield no pairs
 *   (c) Edge: inN < 2 should return empty (k=2 reconstruction guard)
 *
 * These tests directly exercise:
 *   - createSecretShares (token and label shares)
 *   - checkPartitionPairs (token matching via token reconstruction)
 *   - reconstructFromPairs (label recovery from valid pairs)
 */

#include "receiver.h"
extern "C" {
#include "modpoly.h"
}

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <vector>

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
// Test Scenario (a): TP — matching pair is found and label reconstructed
// ────────────────────────────────────────────────────────────────────────────

static void test_tp_matching_pair() {
    printf("\n[Test (a): TP - Matching Pair]\n");

    // Set up small globals: N=8, K=2, T=1 (single token round)
    const int N = 8;
    const int K = 2;
    const long M = FIELD_MODULUS;

    // Save original globals and restore at end
    int saved_inN = inN;
    int saved_token = token;
    inN = N;
    token = 0;  // token value to match

    initializeSharePoly(M);

    // Construct a partition where (i=1, j=2) is the matching pair
    // Token shares: secret=0 (our search value)
    unsigned long* token_shares = createSecretShares(0, N, K, 0);

    // ID shares: secret=42 (the label we expect to recover)
    unsigned long* id_shares = createSecretShares(42, N, K, 0);

    // Scenario: check partition pairs with token shares
    vector<uint64_t> token_partition(token_shares, token_shares + N);
    PairSet pairs = checkPartitionPairs(token_partition);

    // Verify that pair (1,2) was found
    bool pair_12_found = pairs.count({1, 2}) > 0;
    check(pair_12_found, "TP: pair (1,2) found in checkPartitionPairs");

    // Verify that reconstructing (1,2) from id_shares yields 42
    if (pair_12_found) {
        vector<uint64_t> id_partition(id_shares, id_shares + N);
        set<int> reconstructed = reconstructFromPairs(pairs, id_partition);
        bool has_42 = reconstructed.count(42) > 0;
        check(has_42, "TP: reconstructFromPairs recovers label=42");
    } else {
        check(false, "TP: reconstructFromPairs skipped (no pair found)");
    }

    free(token_shares);
    free(id_shares);

    // Restore globals
    inN = saved_inN;
    token = saved_token;
}

// ────────────────────────────────────────────────────────────────────────────
// Test Scenario (b): TN — non-matching partition yields no pairs
// ────────────────────────────────────────────────────────────────────────────

static void test_tn_no_pairs() {
    printf("\n[Test (b): TN - Non-Matching Partition]\n");

    const int N = 8;
    const int K = 2;
    const long M = FIELD_MODULUS;

    // Save and modify globals
    int saved_inN = inN;
    int saved_token = token;
    inN = N;
    token = 0;  // we search for token=0

    initializeSharePoly(M);

    // Create a partition where no pair reconstructs to token=0
    // Build shares for secret=999 (not our target token=0)
    unsigned long* token_shares = createSecretShares(999, N, K, 0);
    vector<uint64_t> token_partition(token_shares, token_shares + N);

    PairSet pairs = checkPartitionPairs(token_partition);

    // With high probability, no pair from this partition will reconstruct to 0
    // (one could construct an exhaustive test, but this catches the common case)
    check(pairs.empty(), "TN: no pairs found for non-matching secret");

    free(token_shares);

    // Restore globals
    inN = saved_inN;
    token = saved_token;
}

// ────────────────────────────────────────────────────────────────────────────
// Test Scenario (c): Edge case — inN < 2 returns empty
// ────────────────────────────────────────────────────────────────────────────

static void test_edge_inN_less_than_2() {
    printf("\n[Test (c): Edge - inN < 2 Guard]\n");

    const int K = 2;
    const long M = FIELD_MODULUS;

    // Save and modify globals
    int saved_inN = inN;
    int saved_token = token;

    initializeSharePoly(M);

    // Test with inN=1 (k=2 reconstruction impossible)
    inN = 1;
    token = 0;

    // Create a partition (though it only has 1 element)
    unsigned long* shares = createSecretShares(42, 1, K, 0);
    vector<uint64_t> partition(shares, shares + 1);

    PairSet pairs = checkPartitionPairs(partition);
    check(pairs.empty(), "inN < 2: checkPartitionPairs returns empty");

    free(shares);

    // Restore globals
    inN = saved_inN;
    token = saved_token;
}

// ────────────────────────────────────────────────────────────────────────────
// Main
// ────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== Protocol Arithmetic Unit Tests ===\n");
    printf("FIELD_MODULUS = %ld\n", FIELD_MODULUS);

    test_tp_matching_pair();
    test_tn_no_pairs();
    test_edge_inN_less_than_2();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);

    // Return non-zero if any test failed (CTest uses exit code)
    return (g_fail > 0) ? 1 : 0;
}
