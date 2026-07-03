// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * test_multi_token_arithmetic.cc — Multi-token FAR amplification & label recovery
 *
 * Tests the FAR amplification mechanism via multi-token rounds:
 *   (a) Spurious elimination: a false pair (i,j) in round 0 but not round 1
 *       should be eliminated by findVerifiedPairs (intersection across T rounds)
 *   (b) TP survival: a genuine pair (i,j) in BOTH rounds survives
 *       findVerifiedPairs and reconstructLabels recovers the correct label
 *
 * These tests directly exercise:
 *   - findVerifiedPairs (multi-round pair intersection)
 *   - reconstructLabels (label reconstruction from verified pairs)
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
// Test Scenario (a): Spurious pair elimination via intersection
// ────────────────────────────────────────────────────────────────────────────

static void test_spurious_elimination() {
    printf("\n[Test (a): Spurious Pair Elimination]\n");

    // Set up globals for two token rounds (T=2)
    const int N = 8;
    const int K = 2;
    const int T = 2;  // Two token rounds
    const long M = FIELD_MODULUS;

    // Save and modify globals
    int saved_inN = inN;
    int saved_token = token;
    inN = N;
    token = 0;  // We search for this token value

    initializeSharePoly(M);

    // Strategy: create two token rounds where pair (1,2) appears in round 0
    // but NOT in round 1 (simulating a spurious false positive in round 0).

    // Round 0: secret=0 with higher probability of spawning spurious pairs
    unsigned long* token_round0 = createSecretShares(0, N, K, 0);
    vector<uint64_t> token_r0_vec(token_round0, token_round0 + N);

    // Round 1: a different secret (e.g., 500) that won't have (1,2) as a valid pair
    unsigned long* token_round1 = createSecretShares(500, N, K, 0);
    vector<uint64_t> token_r1_vec(token_round1, token_round1 + N);

    // Construct the 3D result structure: [token_or_label][partition][subsamples]
    // Here we have T=2 token rounds, so the result will be indexed as:
    //   token_round_results[0][0][p] = token values for partition p from round 0
    //   token_round_results[1][0][p] = token values for partition p from round 1
    UVector3D round0_data(2);  // [token, id]
    round0_data[0].push_back(token_r0_vec);  // partition 0, token channel
    UVector3D round1_data(2);
    round1_data[0].push_back(token_r1_vec);  // partition 0, token channel

    // Create the multi-round structure: token_round_results[T][2][nrof_partitions]
    vector<UVector3D> token_round_results;
    token_round_results.push_back(round0_data);
    token_round_results.push_back(round1_data);

    // Run findVerifiedPairs: computes the intersection of matching pairs across rounds.
    vector<PairSet> verified = findVerifiedPairs(token_round_results);

    PairSet pairs_r0 = checkPartitionPairs(token_r0_vec);
    PairSet pairs_r1 = checkPartitionPairs(token_r1_vec);

    // Deterministic setup: round 0 (secret == token == 0) makes every pair match;
    // round 1 (secret = 500) makes none match. So the cross-round intersection must
    // be empty -- every spurious pair surviving round 0 is eliminated. This is the
    // rho -> rho^T FAR amplification at T=2. No probabilistic branch: the assertions
    // below are always evaluated so a regression in findVerifiedPairs surfaces as a
    // failure rather than a vacuous pass.
    check(!pairs_r0.empty(), "round 0 (secret=0) yields matching pairs");
    check(pairs_r1.empty(),  "round 1 (secret=500) yields no matching pairs");
    check(verified[0].empty(), "intersection eliminates all round-0 spurious pairs");

    // General invariant: any surviving pair must be present in EVERY round.
    bool intersection_invariant = true;
    for (const auto& p : verified[0])
        if (pairs_r0.count(p) == 0 || pairs_r1.count(p) == 0) intersection_invariant = false;
    check(intersection_invariant, "verified pairs are a subset of every round");

    free(token_round0);
    free(token_round1);

    // Restore globals
    inN = saved_inN;
    token = saved_token;
}

// ────────────────────────────────────────────────────────────────────────────
// Test Scenario (b): Genuine pair survives intersection & labels recovered
// ────────────────────────────────────────────────────────────────────────────

static void test_genuine_pair_survival() {
    printf("\n[Test (b): Genuine Pair Survival & Label Recovery]\n");

    const int N = 8;
    const int K = 2;
    const int T = 2;  // Two token rounds
    const long M = FIELD_MODULUS;

    // Save and modify globals
    int saved_inN = inN;
    int saved_token = token;
    inN = N;
    token = 0;  // We search for token=0 across all rounds

    initializeSharePoly(M);

    // Strategy: create two token rounds where the token=0 is shared the same way,
    // so pair (1,2) reconstructs to 0 in BOTH rounds, guaranteeing survival.

    // Round 0: secret=0 (genuine target)
    unsigned long* token_round0 = createSecretShares(0, N, K, 0);
    vector<uint64_t> token_r0_vec(token_round0, token_round0 + N);

    // Round 1: same secret=0 (same genuine target)
    unsigned long* token_round1 = createSecretShares(0, N, K, 0);
    vector<uint64_t> token_r1_vec(token_round1, token_round1 + N);

    // Label shares: secret=99 (the label we expect to recover)
    unsigned long* label_shares = createSecretShares(99, N, K, 0);
    vector<uint64_t> label_vec(label_shares, label_shares + N);

    // Construct result structure
    UVector3D round0_data(2);
    round0_data[0].push_back(token_r0_vec);  // partition 0, token channel
    round0_data[1].push_back(label_vec);     // partition 0, label channel

    UVector3D round1_data(2);
    round1_data[0].push_back(token_r1_vec);  // partition 0, token channel
    round1_data[1].push_back(label_vec);     // partition 0, label channel (same label)

    // Create the multi-round structure
    vector<UVector3D> token_round_results;
    token_round_results.push_back(round0_data);
    token_round_results.push_back(round1_data);

    // Run findVerifiedPairs
    vector<PairSet> verified = findVerifiedPairs(token_round_results);

    // Verify that at least one pair survived the intersection
    bool has_pairs = !verified[0].empty();
    check(has_pairs, "Genuine pairs survive intersection across T=2 rounds");

    // If pairs survived, check that reconstructLabels recovers the label
    if (has_pairs) {
        // reconstructLabels uses the label_result (the UVector3D with id/label parts)
        // Here label_result[1] is the label partition data
        UVector3D label_result(2);
        label_result[1].push_back(label_vec);

        vector<int> recovered = reconstructLabels(verified, label_result);

        bool has_99 = false;
        for (int val : recovered) {
            if (val == 99) {
                has_99 = true;
                break;
            }
        }
        check(has_99, "reconstructLabels recovers label=99 from verified pairs");
    } else {
        check(false, "reconstructLabels skipped (no verified pairs)");
    }

    free(token_round0);
    free(token_round1);
    free(label_shares);

    // Restore globals
    inN = saved_inN;
    token = saved_token;
}

// ────────────────────────────────────────────────────────────────────────────
// Main
// ────────────────────────────────────────────────────────────────────────────

int main() {
    printf("=== Multi-Token-Round FAR Amplification Tests ===\n");
    printf("FIELD_MODULUS = %ld\n", FIELD_MODULUS);

    test_spurious_elimination();
    test_genuine_pair_survival();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);

    // Return non-zero if any test failed (CTest uses exit code)
    return (g_fail > 0) ? 1 : 0;
}
