// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_PLAIN
#define CSTPSI_PLAIN
#include "helper.h"

int countMatches(vector<uint64_t> &a, vector<uint64_t> &b);
UVector3D plaintextSimdPolyEval(UVector2D &simd_query_powers, UVector4D &simd_partition_coeffs);
UVector3D plaintextPolyEval(UVector2D &query_powers, UVector4D &partition_coeffs);
#endif
