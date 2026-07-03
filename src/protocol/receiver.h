// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_RECEIVER
#define CSTPSI_RECEIVER
#include "helper.h"
#include <set>
#include <utility>

using PairSet = std::set<std::pair<uint64_t, uint64_t>>;

UVector2D computeQueryPowers(vector<uint64_t> &que_subsamples);
UVector2D simdQueryPowers(UVector2D &query_powers);
vector<Ciphertext> encryptQueryPowers(SealCredentials &sc, UVector2D &simd_query_powers);
UVector2D extendPartitionRows(UVector2D &decrypted_result_part);
// nrof_real_partitions: if >= 0, drop the phantom SIMD-padding rows (extended
// rows [nrof_real_partitions, end)) before matching. simdPartitions fills the
// unused SIMD lanes with a dummy coefficient; those phantom rows reconstruct
// token=0 as a query-only (round-invariant) function and produce spurious
// matches that survive T-round amplification. -1 keeps all rows (local callers).
UVector3D decryptQueryResult(SealCredentials &sc, CVector2D &encrypted_result_token_id, int nrof_real_partitions = -1);
vector<int> findMatches(UVector3D &result_token_id);

/** k=2 Lagrange reconstruction at x=0 in GF(FIELD_MODULUS). */
int repK2(uint64_t xi, uint64_t xj, uint64_t yi, uint64_t yj);

/** Find matching (i,j) pairs in a partition based on token reconstruction */
PairSet checkPartitionPairs(vector<uint64_t> &token_part_i);

/** Reconstruct matched IDs from known-valid (i,j) pairs */
set<int> reconstructFromPairs(const PairSet &valid_pairs, vector<uint64_t> &id_part_i);

/** Multi-token-round match: find verified (i,j) pairs per partition via intersection across token rounds.
 *  Returns verified_pairs[partition_index] = intersection of pairs from all T token rounds. */
vector<PairSet> findVerifiedPairs(vector<UVector3D> &token_round_results);

/** Label reconstruction using verified pairs from multi-token round.
 *  Returns matched label values for verified pairs. */
vector<int> reconstructLabels(const vector<PairSet> &verified_pairs, UVector3D &label_result);

#endif
