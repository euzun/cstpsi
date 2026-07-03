// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "sender.h"
#include <random>

int mod(int a)
{
  return (FIELD_MODULUS + a) % FIELD_MODULUS;
}

////////////////BUILD DB/////////////////////////////
// A partition's per-position subsamples become the polynomial x-coordinates for
// interpolation, which REQUIRES them distinct. Earlier code enforced this by
// OVERWRITING a colliding subsample with a running counter -- but that destroys an
// enrolled value, so a true-positive query matching the overwritten position loses
// it (a real risk when an enrolled set barely clears the k threshold). Instead, we
// reject the subject from this partition; partitionDB then places it in a different
// (or new) partition. No enrolled value is ever modified, repeated subsamples are
// separated rather than corrupted (the natural biometric case), and -- because
// isDistinct no longer writes the shared MAX_SUB counter -- the former
// partition-thread data race is gone (no lock needed). nrof_collisions is no longer
// consulted: any positional collision => separate partitions.
bool isDistinct(int &pid, I2SSM &enr_ss_map, vector<int> &next_partition)
{
  vector<uint64_t> &pid_subsamps = enr_ss_map[pid];
  int nrof_ids = (int)next_partition.size();
  for (int i = 0; i < nrof_ids; i++)
    if (countMatches(pid_subsamps, enr_ss_map[next_partition[i]]) > 0)
      return false;
  return true;
}

vector<vector<int>> partitionDB(I2SSM &enr_ss_map, vector<int> &rand_db_ind, int &f_ind, int &l_ind)
{
  vector<int> enr_keys;
  for(I2SSM::iterator it = enr_ss_map.begin(); it != enr_ss_map.end(); ++it)
    enr_keys.push_back(it->first);

  // Seed the first partition with the ACTUAL enrolled id, not the raw shuffled
  // index. The loop body below uses id = enr_keys[rand_db_ind[i]]; the seed must
  // match. Storing rand_db_ind[f_ind] directly (a value in [0,actual_enr_count))
  // injects a spurious id and silently drops enr_keys[rand_db_ind[f_ind]], which
  // surfaces as a missed true-positive whenever that dropped subject is queried.
  vector<int> new_partition{enr_keys[rand_db_ind[f_ind]]};
  vector<vector<int>> partitions{new_partition};

  int id;
  bool new_part_flag;
  int bak_part;
  int next_part = -1;
  int nrof_parts = 1;
  for (int i = f_ind + 1; i < l_ind; i++)
  {
    id = enr_keys[rand_db_ind[i]];
    new_part_flag = true;
    for (int j = 0; j < nrof_parts; j++)
    {
      bak_part = next_part;
      next_part = (next_part + 1) % nrof_parts;
      while ((bak_part != next_part) && partitions[next_part].size() >= partition_size)
        next_part = (next_part + 1) % nrof_parts;

      if ((bak_part != next_part) && isDistinct(id, enr_ss_map, partitions[next_part]))
      {
        //no overlapping subsamples. include partitions and break.
        partitions[next_part].push_back(id);
        new_part_flag = false;
        break;
      }
    }
    if (new_part_flag)
    {
      //include new partition
      vector<int> new_partition;
      new_partition.push_back(id);
      partitions.push_back(new_partition);
      nrof_parts++;
      next_part = nrof_parts - 1;
    }
  }
  return partitions;
}

/** split database as such each partition holds non-overlapping subsamples of different subjects.*/
I32Vector3D parallelPartitionDB(I2SSM &enr_ss_map)
{
  // Use the actual map size, not nrof_enr_ids (which is 2^enr_bits and may
  // exceed the number of records actually loaded from the CSV).
  int actual_enr_count = (int)enr_ss_map.size();
  //split indices per thread.
  int increment = actual_enr_count / nrof_splits;
  vector<int> f_ind(nrof_splits);
  vector<int> l_ind(nrof_splits);
  f_ind[0] = 0;
  l_ind[0] = increment;
  for (int i = 1; i < nrof_splits; i++)
  {
    f_ind[i] = l_ind[i - 1];
    l_ind[i] = l_ind[i - 1] + increment;
    // cout<<f_ind[i]<<" - "<<l_ind[i]<<endl;
  }
  l_ind[nrof_splits - 1] = actual_enr_count;

  //randomize subjects in DB for test purpose.
  vector<int> rand_db_ind = rangeVector(0, actual_enr_count);
  { static thread_local mt19937 rng(random_device{}()); std::shuffle(rand_db_ind.begin(), rand_db_ind.end(), rng); };

  I32Vector3D parPartitions(nrof_splits);
  // if(nrof_splits > 1): with a single split (the experiment sets nrof_splits=1)
  // this forked a full thread team for ONE iteration -- a pointless fork whose
  // barrier deadlocked at db=1M on Apple libomp. Run serially when there is
  // nothing to fan out.
  #pragma omp parallel for if(nrof_splits > 1) num_threads(nrof_offline_threads)
  for (int i = 0; i < nrof_splits; i++)
    parPartitions[i] = partitionDB(enr_ss_map, rand_db_ind, f_ind[i], l_ind[i]);

  return parPartitions;
}

UVector3D getSplitCoefficients(vector<int> &partition_ids, I2SSM &enr_ss_map, I2SSM &token_share_map, I2SSM &id_share_map, int tid)
{
  // Thread-local CSPRNG for padding coefficients
  static thread_local std::mt19937_64 rng(std::random_device{}());

  UVector2D x = create2DVector<uint64_t>(inN, partition_size, ZERO);
  UVector2D y_token = create2DVector<uint64_t>(inN, partition_size, ZERO);
  UVector2D y_id = create2DVector<uint64_t>(inN, partition_size, ZERO);

  int current_partition_size = partition_ids.size();
  for (int j = 0; j < current_partition_size; j++)
  {
    int id = partition_ids[j];
    vector<uint64_t> subsamples_j = enr_ss_map[id];
    vector<uint64_t> token_shares_j = token_share_map[id];
    vector<uint64_t> id_shares_j = id_share_map[id];
    for (int i = 0; i < inN; i++)
    {
      x[i][j] = subsamples_j[i];
      y_token[i][j] = token_shares_j[i];
      y_id[i][j] = id_shares_j[i];
    }
  }

  uint64_t dummy_item=MAX_SUB;
  //pad with dummy ids if partition has not enough ids.
  if (current_partition_size < partition_size)
    for (int j = current_partition_size; j < partition_size; j++)
      for (int i = 0; i < inN; i++)
      {
        x[i][j] = (++dummy_item) % FIELD_MODULUS;
        y_token[i][j] = 2 + rng() % (FIELD_MODULUS - 2);
        y_id[i][j] = 2 + rng() % (FIELD_MODULUS - 2);
      }


  // Defensive distinct-x guard. FLINT interpolation requires distinct x-coords
  // per position. Since the DB is now blinded BEFORE partitioning and isDistinct
  // separates ANY positional collision, two REAL subjects in the same partition
  // can no longer share a blinded value at position i -- so for the real slots
  // (j < current_partition_size) this loop is now EXPECTED to find 0 duplicates.
  // It is retained only to dedup the PADDING dummies (j >= current_partition_size,
  // the small ++dummy_item integers, which can coincide with each other or with a
  // real blinded value). Because the colliding slot replaced is always the LATER
  // one and dummies occupy the high slots, a real subject's x is never overwritten.
  for (int i = 0; i < inN; i++)
  {
    vector<uint64_t> xi=x[i];
    unordered_map<int, int> dup_map;
    dup_map[xi[0]] = 1;
    for (int j = 1; j < partition_size; j++)
    {
      unordered_map<int, int>::const_iterator got = dup_map.find(xi[j]);
      if(got==dup_map.end())
      {
        dup_map[xi[j]]=1;
      }
      else
      {
        x[i][j] = (++dummy_item) % FIELD_MODULUS; // duplicate x (expected: only padding dummies now)
        xi[j]=x[i][j];
        dup_map[xi[j]]=1;
      }
    }
  }

  //compute coefficients of interpolating polynomials.
  UVector3D coeffs = create3DVector<uint64_t>(N_CHANNELS, partition_size, inN, ZERO);
  for (int i = 0; i < inN; i++)
  {
    unsigned long *coeffs_token = getInterpolCoeffs((unsigned long *)&x[i][0], (unsigned long *)&y_token[i][0], partition_size, tid);
    unsigned long *coeffs_id = getInterpolCoeffs((unsigned long *)&x[i][0], (unsigned long *)&y_id[i][0], partition_size, tid);
    for (int j = 0; j < partition_size; j++)
    {
      coeffs[0][j][i] = coeffs_token[j];
      coeffs[1][j][i] = coeffs_id[j];
    }
  }

  return coeffs;
}

/** concatenated splits. [partitions ,token and id, partition_size of ids, N subsamples]*/
UVector4D computeCoefficients(I32Vector3D &parPartitions, I2SSM &enr_ss_map, I2SSM &token_share_map, I2SSM &id_share_map)
{
  UVector4D partition_coeffs; // [partitions ,token and id, partition_size of ids, N subsamples]
  for (int i = 0; i < nrof_splits; i++)
  {
    I32Vector2D split_i = parPartitions[i];
    UVector4D partitions_of_split_i(split_i.size());
    #pragma omp parallel for num_threads(nrof_offline_threads)
    for (int j = 0; j < split_i.size(); j++)
      partitions_of_split_i[j] = getSplitCoefficients(split_i[j], enr_ss_map, token_share_map, id_share_map, omp_get_thread_num());
    partition_coeffs.insert(partition_coeffs.end(), partitions_of_split_i.begin(), partitions_of_split_i.end());
  }
  return partition_coeffs;
}

/**return [token and id,simd_partitions, partition_size of coeffs, m concatanation of m/N ids' subsamples.]*/
UVector4D simdPartitions(UVector4D &partition_coeffs)
{
  // reorganize token and id shares while concatenating.
  int nrof_partitions = partition_coeffs.size();
  int nrof_ids_in_row = m / inN;
  int nrof_simd_parts = (int)ceil((1.0 * nrof_partitions) / nrof_ids_in_row);

  // CSPRNG for padding coefficients (called single-threaded, outside OMP)
  static std::mt19937_64 rng(std::random_device{}());

  uint64_t dummy_coeff = 2 + rng() % (FIELD_MODULUS - 2);
  UVector4D simd_partition_coeffs = create4DVector<uint64_t>(N_CHANNELS, nrof_simd_parts, partition_size, m, dummy_coeff);

  int simd_part_ind = 0;
  int m_cnt = 0;
  for (int i = 0; i < nrof_partitions; i++)
  {
    simd_part_ind = i / nrof_ids_in_row;
    m_cnt %= m;
    for (int j = 0; j < inN; j++)
    {
      for (int k = 0; k < partition_size; k++)
      {
        simd_partition_coeffs[0][simd_part_ind][k][m_cnt] = partition_coeffs[i][0][k][j];
        simd_partition_coeffs[1][simd_part_ind][k][m_cnt] = partition_coeffs[i][1][k][j];
      }
      m_cnt++;
    }
  }
  return simd_partition_coeffs;
}

PVector3D encodeCoeffs(SealCredentials &sc, UVector4D &simd_partition_coeffs)
{
  BatchEncoder batch_encoder(*sc.context);

  int iS = simd_partition_coeffs.size();
  int jS = simd_partition_coeffs[0].size();
  int kS = simd_partition_coeffs[0][0].size();

  Plaintext temp_plain;
  PVector3D encoded_coeffs = create3DVector<Plaintext>(iS, jS, kS, temp_plain);
  #pragma omp parallel for collapse(3) num_threads(nrof_offline_threads)
  for (int i = 0; i < iS; i++)
    for (int j = 0; j < jS; j++)
      for (int k = 0; k < kS; k++)
        batch_encoder.encode(simd_partition_coeffs[i][j][k], encoded_coeffs[i][j][k]);

  return encoded_coeffs;
}

////////////////LPSI/////////////////////////////
void computePolyModSwitch(SealCredentials &sc, CVector2D &encrypted_result_token_id)
{
  Evaluator evaluator(*sc.context);
  int nrof_parts = encrypted_result_token_id[0].size();

  auto context_data = sc.context->first_context_data(); //SEAL 3.5
  while (context_data->next_context_data())
  {
  #pragma omp parallel for collapse(2) num_threads(nrof_online_threads)
    for (int i = 0; i < 2; i++)
      for (int j = 0; j < nrof_parts; j++)
        evaluator.mod_switch_to_next_inplace(encrypted_result_token_id[i][j]);
    context_data = context_data->next_context_data();
  }
}

Ciphertext evalPartition(Evaluator &evaluator, vector<Ciphertext> &encrypted_query_powers, vector<Plaintext> &encoded_part_coeffs)
{
  vector<Ciphertext> pow_terms(partition_size - 1);
  // #pragma omp parallel for collapse(2) num_threads(nrof_online_threads)
  for (int i = 0; i < partition_size - 1; i++)
    evaluator.multiply_plain(encrypted_query_powers[i], encoded_part_coeffs[i + 1], pow_terms[i]);
  Ciphertext encrypted_result;
  evaluator.add_many(pow_terms, encrypted_result);
  evaluator.add_plain_inplace(encrypted_result, encoded_part_coeffs[0]);
  return encrypted_result;
}

/** Function homomorphically evaluating the interpolating function. */
CVector2D homomorphicPolyEval(SealCredentials &sc, vector<Ciphertext> &encrypted_query_powers, PVector3D &encoded_coeffs)
{
  Evaluator evaluator(*sc.context);
  int nrof_parts = encoded_coeffs[0].size();
  Ciphertext temp_cipher;
  CVector2D encrypted_result_token_id = create2DVector<Ciphertext>(N_CHANNELS, nrof_parts, temp_cipher);

  #pragma omp parallel for collapse(2) num_threads(nrof_online_threads)
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < nrof_parts; j++)
      encrypted_result_token_id[i][j] = evalPartition(evaluator, encrypted_query_powers, encoded_coeffs[i][j]);

  computePolyModSwitch(sc, encrypted_result_token_id);
  return encrypted_result_token_id;
}
