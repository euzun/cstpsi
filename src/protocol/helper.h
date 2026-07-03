// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_HELPER
#define CSTPSI_HELPER
#include <boost/algorithm/string.hpp>
#include "params.h"

using I2SSM = unordered_map<int, vector<uint64_t>>;  // subsample and secret-shares map. key is id.

template <typename K>
vector<vector<K>> create2DVector(int &s2, int &s1, K init)
{
  vector<vector<K>> _2Dvector;
  if (s1 > 0)
  {
    _2Dvector.reserve(s2 * s1);
    for (int i = 0; i < s2; i++)
    {
      vector<K> _1Dvector(s1, init);
      _2Dvector.push_back(_1Dvector);
    }
  }
  else
    for (int i = 0; i < s2; i++)
    {
      vector<K> _1Dvector;
      _2Dvector.push_back(_1Dvector);
    }
  return _2Dvector;
}

template <typename K>
vector<vector<vector<K>>> create3DVector(int &s3, int &s2, int &s1, K init)
{
  vector<vector<vector<K>>> _3Dvector;
  if (s1 > 0)
    _3Dvector.reserve(s3 * s2 * s1);
  for (int i = 0; i < s3; i++)
  {
    _3Dvector.push_back(create2DVector(s2, s1, init));
  }
  return _3Dvector;
}

template <typename K>
vector<vector<vector<vector<K>>>> create4DVector(int &s4, int &s3, int &s2, int &s1, K init)
{
  vector<vector<vector<vector<K>>>> _4Dvector;
  if (s1 > 0)
    _4Dvector.reserve(s4 * s3 * s2 * s1);
  for (int i = 0; i < s4; i++)
  {
    _4Dvector.push_back(create3DVector(s3, s2, s1, init));
  }
  return _4Dvector;
}

template <typename K>
vector<vector<vector<vector<vector<K>>>>> create5DVector(int &s5, int &s4, int &s3, int &s2, int &s1, K init)
{
  vector<vector<vector<vector<vector<K>>>>> _5Dvector;
  if (s1 > 0)
    _5Dvector.reserve(s5 * s4 * s3 * s2 * s1);
  for (int i = 0; i < s5; i++)
  {
    _5Dvector.push_back(create4DVector(s4, s3, s2, s1, init));
  }
  return _5Dvector;
}

/** generate consequtive integer vector in range of [_f,_l)*/
vector<int> rangeVector(int _f, int _l);

/**
 * Modular multiplicative inverse of a modulo FIELD_MODULUS.
 * Uses the extended Euclidean algorithm.
 * Requires gcd(a, FIELD_MODULUS) == 1 (always true when FIELD_MODULUS is prime
 * and 0 < a < FIELD_MODULUS).
 */
uint64_t field_mod_inverse(uint64_t a);


#endif
