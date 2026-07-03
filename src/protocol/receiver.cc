// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "receiver.h"
#include <cstdlib>

#ifdef _OPENMP
#include <omp.h>
// Parallelize a region only when NOT already inside one. The Deep1B experiment
// parallelizes its outer per-query loop, so these inner regions must run serially
// there (avoids nested-region forks, which on Apple libomp can deadlock a later
// barrier). The serial LFW / network paths call these at top level -> still parallel.
#define OMP_TOPLEVEL (!omp_in_parallel())
#else
#define OMP_TOPLEVEL 1
#endif


////////////QUERYING SUBSAMPLES/////////////////////////////////
/** degree y^1 to y^(partition_size-1) */
UVector2D computeQueryPowers(vector<uint64_t> &que_subsamples)
{
  int degree=partition_size;
  UVector2D query_powers=create2DVector<uint64_t>(degree,inN,ZERO);
  query_powers[0]=que_subsamples;

  for(int i=1;i<degree;i++)
  {
    vector<uint64_t> subsamples_pre_pow=query_powers[i-1];
    // #pragma omp parallel for num_threads(nrof_online_threads)
    for(int j=0;j<inN;j++)
      query_powers[i][j]=((uint64_t)(subsamples_pre_pow[j]*que_subsamples[j]))%FIELD_MODULUS;
  }

  return query_powers;
}

UVector2D simdQueryPowers(UVector2D &query_powers)
{
  int degree=partition_size;
  UVector2D simd_query_powers=create2DVector<uint64_t>(degree,m,ONE);

  int nrof_ids_in_row=m/inN;
  int cnt=0;
  for(int i=0;i<degree;i++)
  {
    vector<uint64_t> query_power_i=query_powers[i];
    cnt=0;
    for(int j=0;j<nrof_ids_in_row;j++)
      for(int k=0;k<inN;k++)
        simd_query_powers[i][cnt++]=query_power_i[k];
  }
  // cout<<simd_query_powers[0][1]<<" - "<<simd_query_powers[0][1+inN]<<endl;
  return simd_query_powers;
}

vector<Ciphertext> encryptQueryPowers(SealCredentials &sc, UVector2D &simd_query_powers)
{
  Encryptor encryptor(*sc.context, sc.public_key);
  BatchEncoder batch_encoder(*sc.context);

  int degree=partition_size;
  vector<Ciphertext> encrypted_query_powers(degree);

  #pragma omp parallel for num_threads(nrof_online_threads)
  for(int i=0;i<degree;i++)
  {
    Plaintext plain_matrix;
    batch_encoder.encode(simd_query_powers[i],plain_matrix);
    encryptor.encrypt(plain_matrix, encrypted_query_powers[i]);
  }

  return encrypted_query_powers;
}

///// DECRYPT RESULT//////////
UVector2D extendPartitionRows(UVector2D &decrypted_result_part)
{
  int nrof_rows_in_part=m/inN;
  int nrof_parts=decrypted_result_part.size();
  int nrof_extended_parts=nrof_parts*nrof_rows_in_part;
  UVector2D extended_parts=create2DVector<uint64_t>(nrof_extended_parts, inN, ZERO);

  int c=0;
  int m_cnt=0;
  for(int i=0;i<nrof_parts;i++)
  {
    m_cnt=0;
    vector<uint64_t> row_i=decrypted_result_part[i];
    for(int j=0;j<nrof_rows_in_part;j++)
    {
      for(int k=0;k<inN;k++)
        extended_parts[c][k]=row_i[m_cnt++];
      c++;
    }
  }
  return extended_parts;
}

vector<uint64_t> decryptPartitionResult(Decryptor &decryptor, BatchEncoder &batch_encoder, Ciphertext &encrypted_result)
{
  vector<uint64_t> decrypted_result;
  Plaintext plain_matrix;
  decryptor.decrypt(encrypted_result,plain_matrix);
  batch_encoder.decode(plain_matrix,decrypted_result);
  return decrypted_result;
}

UVector3D decryptQueryResult(SealCredentials &sc, CVector2D &encrypted_result_token_id, int nrof_real_partitions)
{
  Decryptor decryptor(*sc.context, sc.secret_key);
  BatchEncoder batch_encoder(*sc.context);

  int nrof_parts=encrypted_result_token_id[0].size();
  UVector3D decrypted_result_token_id=create3DVector<uint64_t>(N_CHANNELS, nrof_parts, ZERO, ZERO);

  #pragma omp parallel for collapse(2) num_threads(nrof_online_threads)
  for(int i=0;i<2;i++)
    for(int j=0;j<nrof_parts;j++)
      decrypted_result_token_id[i][j]=decryptPartitionResult(decryptor, batch_encoder,encrypted_result_token_id[i][j]);

  UVector3D reformed_result_token_id{extendPartitionRows(decrypted_result_token_id[0]), extendPartitionRows(decrypted_result_token_id[1])};

  // Drop phantom SIMD-padding rows: the SIMD packing yields m/inN rows per
  // ciphertext-part, but only nrof_real_partitions of them hold real subjects.
  // The rest are dummy-coefficient padding that reconstructs token=0 as a
  // query-only function (independent of the per-round secret shares), so they
  // would survive token-round amplification and cause spurious matches. See
  // findVerifiedPairs/findMatches. -1 = keep all (local/legacy callers).
  if (nrof_real_partitions >= 0) {
    int total_rows = (int)reformed_result_token_id[0].size();
    if (nrof_real_partitions < total_rows) {
      reformed_result_token_id[0].resize(nrof_real_partitions);
      reformed_result_token_id[1].resize(nrof_real_partitions);
    }
  }

  return reformed_result_token_id;
}


// Cache of modular inverses of small position-differences. Every k=2
// reconstruction's Lagrange denominator is (xj - xi) with xi<xj and both in
// [1, inN], so (xj - xi) takes only inN-1 distinct values. The table is a
// magic-static (thread-safe one-time init, then read-only -- safe under the
// parallel query loop) built once and reused, replacing billions of
// extended-Euclid inversions with table lookups. Result is identical.
// PRECONDITION: inN and FIELD_MODULUS must hold their final values before the
// first call (set once at process startup) and must not change afterward; a
// stale table would return wrong inverses. If inN<=0 at first call the table
// is size 1 and every call uses the (correct, slow) field_mod_inverse fallback.
static uint64_t smallDiffInverse(uint64_t d)
{
  // Ablation switch: CSTPSI_DISABLE_INV_CACHE=1 forces a per-call field inversion
  // (the unoptimized base) so a paired on/off run measures the inverse cache's
  // saving directly (the "cached reconstruction" step in the optimization breakdown).
  static const bool disable = []{ const char* e = std::getenv("CSTPSI_DISABLE_INV_CACHE");
                                  return e && e[0] && e[0] != '0'; }();
  if (disable) return field_mod_inverse(d);
  static const std::vector<uint64_t> cache = []{
    std::vector<uint64_t> c(inN > 0 ? inN : 1, 0);
    for (int x = 1; x < inN; ++x) c[x] = field_mod_inverse((uint64_t)x);
    return c;
  }();
  if (d > 0 && d < cache.size()) return cache[d];
  return field_mod_inverse(d);  // safety fallback (callers never hit this)
}

int repK2(uint64_t xi, uint64_t xj, uint64_t yi, uint64_t yj)
{
  // Lagrange interpolation at x=0 for two points (xi,yi),(xj,yj) in GF(FIELD_MODULUS):
  //   f(0) = (yi*xj - yj*xi) * modinv(xj-xi)  mod M
  //
  // Use safe modular subtraction to avoid uint64_t underflow,
  // and modular inverse instead of integer division.
  uint64_t M = (uint64_t)FIELD_MODULUS;
  uint64_t a = (yi % M) * (xj % M) % M;
  uint64_t b = (yj % M) * (xi % M) % M;
  uint64_t num = (a + M - b) % M;
  uint64_t denom_inv = smallDiffInverse(xj - xi);  // xi<xj, xj-xi in [1, inN-1]
  return (int)(num * denom_inv % M);
}

PairSet checkPartitionPairs(vector<uint64_t> &token_part_i)
{
  PairSet pairs;
  // Guard: k=2 reconstruction requires at least 2 points; if inN < 2, no valid pairs
  if (inN < 2) return pairs;

  for (uint64_t i = 1; i <= inN - 1; i++) {
    uint64_t yi = token_part_i[i - 1];
    for (uint64_t j = i + 1; j <= inN; j++) {
      int rep_token = repK2(i, j, yi, token_part_i[j - 1]);
      if (rep_token == token)
        pairs.insert({i, j});
    }
  }
  return pairs;
}

set<int> reconstructFromPairs(const PairSet &valid_pairs, vector<uint64_t> &id_part_i)
{
  set<int> matches;
  for (auto& [i, j] : valid_pairs) {
    matches.insert(repK2(i, j, id_part_i[i - 1], id_part_i[j - 1]));
  }
  return matches;
}

vector<PairSet> findVerifiedPairs(vector<UVector3D> &token_round_results)
{
  int T = token_round_results.size();
  int nrof_parts = token_round_results[0][0].size();  // [token_or_id=0][partition]

  vector<PairSet> verified(nrof_parts);

  #pragma omp parallel for if(OMP_TOPLEVEL) num_threads(nrof_online_threads)
  for (int p = 0; p < nrof_parts; p++) {
    // Start with pairs from first token round
    verified[p] = checkPartitionPairs(token_round_results[0][0][p]);

    // Intersect with subsequent token rounds
    for (int t = 1; t < T; t++) {
      PairSet round_pairs = checkPartitionPairs(token_round_results[t][0][p]);
      PairSet intersection;
      for (auto& pair : verified[p]) {
        if (round_pairs.count(pair))
          intersection.insert(pair);
      }
      verified[p] = std::move(intersection);
    }
  }

  return verified;
}

vector<int> reconstructLabels(const vector<PairSet> &verified_pairs, UVector3D &label_result)
{
  int nrof_parts = label_result[1].size();  // id part
  vector<set<int>> label_sets(nrof_parts);

  #pragma omp parallel for if(OMP_TOPLEVEL) num_threads(nrof_online_threads)
  for (int p = 0; p < nrof_parts; p++) {
    label_sets[p] = reconstructFromPairs(verified_pairs[p], label_result[1][p]);
  }

  vector<int> all_labels;
  for (int p = 0; p < nrof_parts; p++) {
    for (int val : label_sets[p])
      all_labels.push_back(val);
  }
  return all_labels;
}

set<int> checkPartition(vector<uint64_t> &token_part_i, vector<uint64_t> &id_part_i)
{
  int rep_token;
  set<int> matches;
  // Guard: k=2 reconstruction requires at least 2 points; if inN < 2, no valid matches
  if (inN < 2) return matches;

  for(uint64_t i=1;i<=inN-1;i++)
  {
    uint64_t yi=token_part_i[i-1];
    for(uint64_t j=i+1;j<=inN;j++)
    {
      rep_token=repK2(i,j,yi,token_part_i[j-1]);
      if(rep_token==token)
        matches.insert(repK2(i,j,id_part_i[i-1],id_part_i[j-1]));
    }
  }
  return matches;
}

vector<int> findMatches(UVector3D &result_token_id)
{
  int nrof_parts=result_token_id[0].size();
  vector<set<int>> match_sets(nrof_parts);

  #pragma omp parallel for if(OMP_TOPLEVEL) num_threads(nrof_online_threads)
  for(int i=0;i<nrof_parts;i++)
    match_sets[i]=checkPartition(result_token_id[0][i], result_token_id[1][i]);


  vector<int> matching_ids;
  for(int i=0;i<nrof_parts;i++)
  {
    set<int> matches=match_sets[i];
    if(matches.size()>0)
      for (set<int>::iterator it=matches.begin(); it!=matches.end(); ++it)
        matching_ids.push_back(*it);
  }
  return matching_ids;
}

