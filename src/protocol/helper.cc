// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "helper.h"


vector<int> rangeVector(int _f, int _l)
{
  int s=_l-_f;
  vector<int> indices(s);
  for(int i=0;i<s;i++)
    indices[i]=i+_f;
  return indices;
}

uint64_t field_mod_inverse(uint64_t a)
{
  // Extended Euclidean algorithm: find x s.t. a*x ≡ 1 (mod FIELD_MODULUS)
  long t = 0, newt = 1;
  long r = FIELD_MODULUS, newr = (long)(a % (uint64_t)FIELD_MODULUS);
  while (newr != 0) {
    long q = r / newr;
    long tmp = t - q * newt; t = newt; newt = tmp;
    long tmp2 = r - q * newr; r = newr; newr = tmp2;
  }
  if (t < 0) t += FIELD_MODULUS;
  return (uint64_t)t;
}
