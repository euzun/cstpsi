// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Copyright 2020 Erkam Uzun
 * IISP@Georgia Institute of Technology
 * This file is part of basat: privacy preserving and labeled biometric fuzzy search
 * 8/10/2020
 */
#ifndef CSTPSI_MODPOLY
#define CSTPSI_MODPOLY

void initializeSharePoly(long _pol_modulus);
unsigned long *createSecretShares(int secret, int N, int K, int tid);
/**function to initialize interpolating polynomial which has a polynomial modulo of pol_modulus */
void initializePolynomial(long pol_modulus);
unsigned long *getInterpolCoeffs(unsigned long *xs, unsigned long *ys, long pows, int tid);
#endif
