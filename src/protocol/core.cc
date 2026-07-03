// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
// CSTPSI 2026
#include "receiver.h"
#include "sender.h"

I2SSM token_share_map; // id-> vector of token shares
I2SSM id_share_map;    // id-> vector of id shares

void setShares(int pid)
{
  unsigned long *token_shares = createSecretShares(token, inN, inK, 0);
  unsigned long *id_shares = createSecretShares(pid, inN, inK, 1);
  for (int i = 0; i < inN; i++)
  {
    token_share_map[pid].push_back(token_shares[i]);
    id_share_map[pid].push_back(id_shares[i]);
  }
}


vector<int> submitSingleQuery(SealCredentials &sc, vector<uint64_t> &que_subsamples, PVector3D &encoded_coeffs)
{
  UVector2D query_powers = computeQueryPowers(que_subsamples);
  UVector2D simd_query_powers = simdQueryPowers(query_powers);
  vector<Ciphertext> encrypted_query_powers = encryptQueryPowers(sc, simd_query_powers);
  CVector2D encrypted_result_token_id = homomorphicPolyEval(sc, encrypted_query_powers, encoded_coeffs);
  UVector3D reformed_result_token_id = decryptQueryResult(sc, encrypted_result_token_id);
  vector<int> matching_ids = findMatches(reformed_result_token_id);
  return matching_ids;
}

