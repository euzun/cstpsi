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

#include "modpoly.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <gmp.h>
#include <flint/nmod_poly.h>
#include <fcntl.h>
#include <unistd.h>

#define nrof_thr 72
nmod_poly_t _poly_share[nrof_thr];
nmod_poly_t _poly_vec[nrof_thr];
long pol_modulus;

// Thread-local CSPRNG state for Shamir polynomial coefficients
static _Thread_local uint64_t _csprng_state = 0;

// Initialize CSPRNG state once per thread via arc4random_buf or /dev/urandom
// When _csprng_state is 0 (sentinel), we seed from a CSPRNG. The probability
// of collision or zero-seed in mid-stream reseed is ~2^-64 and is harmless.
static void _init_csprng_state(void) {
  if (_csprng_state == 0) {
    uint64_t seed = 0;
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
    arc4random_buf(&seed, sizeof(seed));
#else
    /* Linux/other: /dev/urandom is a CSPRNG available on all glibc versions */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
      ssize_t got = read(fd, &seed, sizeof(seed));
      (void)got;
      close(fd);
    }
    /* if open/read fails, seed stays 0 and the sentinel below bumps it to 1 */
#endif
    // Ensure seed is never 0 (we use 0 as sentinel for uninitialized)
    _csprng_state = (seed == 0) ? 1 : seed;
  }
}

// Simple splitmix64 mixing function for deterministic derivation from state
static uint64_t _splitmix64(uint64_t *state) {
  uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

// Get next CSPRNG value in [2, pol_modulus - 1]
static unsigned long _next_csprng_coeff(void) {
  _init_csprng_state();
  uint64_t val = _splitmix64(&_csprng_state);
  return 2 + (val % (pol_modulus - 2));
}

void initializeSharePoly(long _pol_modulus)
{
  pol_modulus=_pol_modulus;
  for (int i = 0; i < nrof_thr; i++)
    nmod_poly_init(_poly_share[i], _pol_modulus);
}

unsigned long *createSecretShares(int secret, int N, int K, int tid)
{
  nmod_poly_set_coeff_ui(_poly_share[tid], 0, secret);
  int i;
  for(i=1;i<K;i++)
    nmod_poly_set_coeff_ui(_poly_share[tid], i, _next_csprng_coeff());

  mp_limb_t *c = malloc(sizeof(mp_limb_t) * (N));
  for(i=0;i<N;i++)
    c[i]=nmod_poly_evaluate_nmod(_poly_share[tid], i + 1);

  return c;
}


void initializePolynomial(long pol_modulus)
{
  for (int i = 0; i < nrof_thr; i++)
    nmod_poly_init(_poly_vec[i], pol_modulus);
}

unsigned long *getInterpolCoeffs(unsigned long *xs, unsigned long *ys, long pows, int tid)
{


  // if(tid==0)
  // {
  //   // gmp_printf("%Mu - \n", ys[10]);
  //   // for(int i=0;i<pows;i++)
  //   //   gmp_printf("%Mu - ", ys[i]);
  //   // printf("\n");

  //   char *yc = 'x';
  //   nmod_poly_print_pretty(_poly_vec[tid], &yc);
  //   printf("\n");
  // }

  // nmod_poly_interpolate_nmod_vec(_poly_vec[tid], xs, ys, pows);
  nmod_poly_interpolate_nmod_vec_fast(_poly_vec[tid], xs, ys, pows);
  mp_limb_t *c = malloc(sizeof(mp_limb_t) * (pows));
  int i;
  for (i = 0; i < pows; i++)
    c[i] = nmod_poly_get_coeff_ui(_poly_vec[tid], i);

  return c;
}
