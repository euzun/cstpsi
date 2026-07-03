// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_SENDER
#define CSTPSI_SENDER
#include "plain.h"
extern "C"
{
#include "modpoly.h"
}

void setShares(int pid);
I32Vector3D parallelPartitionDB(I2SSM &enr_ss_map);
UVector4D computeCoefficients(I32Vector3D &parPartitions, I2SSM &enr_ss_map, I2SSM &token_share_map, I2SSM &id_share_map);
UVector4D simdPartitions(UVector4D &partition_coeffs);
PVector3D encodeCoeffs(SealCredentials &sc, UVector4D &simd_partition_coeffs);
CVector2D homomorphicPolyEval(SealCredentials &sc, vector<Ciphertext> &encrypted_query_powers, PVector3D &encoded_coeffs);
#endif
