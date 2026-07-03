// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "plain.h"

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

/** Count matched subsamples between vectors a and b*/
int countMatches(vector<uint64_t> &a, vector<uint64_t> &b)
{
  int cnt = 0;
  for (int i = 0; i < inN; i++)
  {
    // printf("%lu - %lu\n", a[i], b[i]);
    cnt += 1 * (a[i] == b[i]);
  }

  return cnt;
}

vector<uint64_t> evalSimdPartition(UVector2D &simd_query_powers, UVector2D &partition_coeffs_i)
{
  vector<uint64_t> result=partition_coeffs_i[0];
  for(int i=1;i<partition_size;i++)
  {
    vector<uint64_t> pows_i=simd_query_powers[i-1];
    vector<uint64_t> coeffs_i=partition_coeffs_i[i];
    for(int j=0;j<m;j++)
      result[j]=((pows_i[j]*coeffs_i[j])%FIELD_MODULUS+result[j])%FIELD_MODULUS;
  }
  return result;
}

UVector3D plaintextSimdPolyEval(UVector2D &simd_query_powers, UVector4D &simd_partition_coeffs)
{
  int nrof_parts=simd_partition_coeffs[0].size();
  UVector3D plaintext_result_token_id=create3DVector<uint64_t>(N_CHANNELS, nrof_parts, m, ZERO);

  // #pragma omp parallel for collapse(2) num_threads(nrof_online_threads)
  for(int i=0;i<2;i++)
    for(int j=0;j<nrof_parts;j++)
      plaintext_result_token_id[i][j]=evalSimdPartition(simd_query_powers,simd_partition_coeffs[i][j]);
  return plaintext_result_token_id;
}

vector<uint64_t> evalPartition(UVector2D &query_powers, UVector2D &partition_coeffs_i)
{
  vector<uint64_t> result=partition_coeffs_i[0];
  for(int i=1;i<partition_size;i++)
  {
    vector<uint64_t> pows_i=query_powers[i-1];
    vector<uint64_t> coeffs_i=partition_coeffs_i[i];
    for(int j=0;j<inN;j++)
      result[j]=((pows_i[j]*coeffs_i[j])%FIELD_MODULUS+result[j])%FIELD_MODULUS;
  }
  return result;
}

UVector3D plaintextPolyEval(UVector2D &query_powers, UVector4D &partition_coeffs)
{
  int nrof_parts=partition_coeffs.size();
  UVector3D plaintext_result_token_id=create3DVector<uint64_t>(N_CHANNELS, nrof_parts, m, ZERO);

  // Safe to parallelize: each [i][j] cell is written once from read-only inputs,
  // so the result is identical to the serial order. if(OMP_TOPLEVEL): when the
  // experiment already parallelizes the per-query loop, run this serially (no
  // nested fork).
  #pragma omp parallel for collapse(2) if(OMP_TOPLEVEL) num_threads(nrof_online_threads)
  for(int i=0;i<2;i++)
    for(int j=0;j<nrof_parts;j++)
      plaintext_result_token_id[i][j]=evalPartition(query_powers,partition_coeffs[j][i]);
  return plaintext_result_token_id;
}
