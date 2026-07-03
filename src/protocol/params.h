// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_PARAMS
#define CSTPSI_PARAMS
#include <stdlib.h> /* srand, rand */
#include <gmp.h>
#include <omp.h>
#include <algorithm>
#include <cstddef>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include <mutex>
#include <memory>
#include <limits>
#include <set>
#include "seal/seal.h"
using namespace std;
using namespace std::chrono;
using namespace seal;

typedef vector<vector<uint64_t>> UVector2D;
typedef vector<UVector2D> UVector3D;
typedef vector<UVector3D> UVector4D;
typedef vector<UVector4D> UVector5D;

typedef vector<vector<int64_t>> IVector2D;
typedef vector<IVector2D> IVector3D;
typedef vector<IVector3D> IVector4D;
typedef vector<IVector4D> IVector5D;

typedef vector<vector<int>> I32Vector2D;
typedef vector<I32Vector2D> I32Vector3D;
typedef vector<I32Vector3D> I32Vector4D;
typedef vector<I32Vector4D> I32Vector5D;

typedef vector<vector<Plaintext>> PVector2D;
typedef vector<PVector2D> PVector3D;
typedef vector<PVector3D> PVector4D;

typedef vector<vector<Ciphertext>> CVector2D;
typedef vector<CVector2D> CVector3D;
typedef vector<CVector3D> CVector4D;
typedef shared_ptr<SEALContext> SealPtr;

// Protocol mode enum (CSTPSI = fully optimized; STLPSI = unoptimized baseline)
enum class ProtocolMode {
    CSTPSI,  // Single bundled GC + send-once query caching
    STLPSI   // Per-round GC + re-send query each round
};

// Configurable parameters (read from JSON in param_loader.cc)
extern int nrof_que_ids;   //number of queries to issue against the database.
extern int inN;            //number of subsamples (32-64-96-128).
extern int m;              //SIMD batch size. (1024, 2048, 4096)
extern int partition_size; //number of ids each partition has. max degree of query = partition_size. (e.g.128)
extern int nrof_splits;    //number of splits we divide DB.
extern int nrof_collisions;//number of senders' hash bins. removes duplicates. (16, 32, 64, 128, 256 or max.)
// Derived parameters
extern int nrof_enr_ids;   //number of enrolled people (= 2^enr_bits)
extern int nrof_enr_total; //actual enrollment count in file (may differ from 2^enr_bits)
extern uint64_t MAX_SUB;   //max value for subsample (set per data or to FIELD_MODULUS-1)
// Threshold + runtime
extern int inK;            //number of matching subsamples to accept query match (k=2 hardcoded).
extern int nrof_online_threads;
extern int nrof_offline_threads;
extern long FIELD_MODULUS;
extern int token;
// Utility
extern int ZERO;
extern int ONE;
extern int N_CHANNELS;  // Number of channels (token + label) = 2


class SealCredentials
{
public:
  SealPtr context;
  seal::PublicKey public_key;
  seal::SecretKey secret_key;
  seal::RelinKeys relin_keys;
  SealCredentials(EncryptionParameters &_params, int _pmd, uint64_t _fp)
  {
    _params.set_poly_modulus_degree(_pmd);
    // _params.set_coeff_modulus(DefaultParams::coeff_modulus_128(_pmd));//SEAL 3.2
    _params.set_coeff_modulus(CoeffModulus::BFVDefault(_pmd));//SEAL 3.5
    _params.set_plain_modulus(_fp);
    context = make_shared<SEALContext>(_params);
    KeyGenerator keygen(*context);
    keygen.create_public_key(public_key);
    secret_key = keygen.secret_key();
    // relin_keys = keygen.relin_keys(30);//SEAL 3.2
    // cout<<"DEBUG"<<endl;
    // relin_keys = keygen.relin_keys_local();//SEAL 3.5
  }
};
#endif
