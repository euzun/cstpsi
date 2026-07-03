// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Unit tests for k=2 Shamir secret sharing.
 *
 * Covers:
 *   - createSecretShares: degree-1 polynomial evaluated at x=1..N
 *   - repK2: Lagrange reconstruction at x=0 in GF(FIELD_MODULUS)
 *
 * Bug regression cases:
 *   - Bug 1 (underflow): cases where yj*xi > yi*xj as uint64_t
 *   - Bug 2 (integer division): cases where secret*(xj-xi) >= FIELD_MODULUS
 */

#include "receiver.h"
#include "helper.h"
extern "C" {
#include "modpoly.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

// ── minimal mock globals required by helper.cc / params.cc ──────────────────
// (we link cstpsi_params, so all globals are already defined there;
//  we only need to set the ones used by the functions under test)
// FIELD_MODULUS is defined in params.cc = 8519681
// ────────────────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char *label)
{
  if (cond) {
    printf("  PASS  %s\n", label);
    g_pass++;
  } else {
    printf("  FAIL  %s\n", label);
    g_fail++;
  }
}

// ── helpers ──────────────────────────────────────────────────────────────────

// Evaluate f(x) = secret + r*x  mod M at a given x (integer arithmetic).
static uint64_t eval_linear(uint64_t secret, uint64_t r, uint64_t x, uint64_t M)
{
  return (secret % M + (r % M) * (x % M) % M) % M;
}

// ── Test: createSecretShares produces correct polynomial evaluations ──────────

static void test_createSecretShares_basic()
{
  printf("\n[createSecretShares]\n");

  const long M = FIELD_MODULUS;
  initializeSharePoly(M);

  // For each test we generate N shares and verify that any 2 shares
  // reconstruct the original secret via repK2.
  struct TC { int secret; int N; };
  TC cases[] = {
    {0,    4},
    {1,    4},
    {42,   4},
    {1000, 8},
    // stress: secret close to M/inN boundary
    {946630, 8},   // 946630 * 7 = 6626410 < M=8519681  ← would work with old bug too
    {946632, 8},   // 946632 * 8 = 7573056 < M           ← old bug survives
    {2000000, 4},  // 2000000 * 3 = 6000000 < M          ← old bug survives
    {3000000, 3},  // 3000000 * 2 = 6000000 < M          ← old bug survives
    {4000000, 3},  // 4000000 * 2 = 8000000 < M=8519681  ← marginal
    {4300000, 3},  // 4300000 * 2 = 8600000 > M          ← old Bug 2 would FAIL
    {8519680, 4},  // M-1
  };

  for (auto &tc : cases) {
    unsigned long *shares = createSecretShares(tc.secret, tc.N, 2, 0);

    // Each share: shares[i] = f(i+1) mod M
    // Reconstruct with every pair (xi=i+1, xj=j+1) and check == secret
    bool all_ok = true;
    for (int i = 0; i < tc.N - 1; i++) {
      for (int j = i + 1; j < tc.N; j++) {
        uint64_t xi = (uint64_t)(i + 1);
        uint64_t xj = (uint64_t)(j + 1);
        uint64_t yi = shares[i];
        uint64_t yj = shares[j];
        int rec = repK2(xi, xj, yi, yj);
        if (rec != tc.secret % (int)M) {
          printf("    secret=%d N=%d pair(%d,%d): got %d, want %d  yi=%lu yj=%lu\n",
                 tc.secret, tc.N, i+1, j+1, rec, tc.secret % (int)M,
                 (unsigned long)yi, (unsigned long)yj);
          all_ok = false;
        }
      }
    }
    free(shares);

    char label[80];
    snprintf(label, sizeof(label), "secret=%d N=%d all pairs reconstruct", tc.secret, tc.N);
    check(all_ok, label);
  }
}

// ── Test: repK2 with hand-crafted shares that trigger Bug 1 ──────────────────
// Bug 1: yi*xj < yj*xi as uint64_t (underflow in naive code)

static void test_repK2_underflow_regression()
{
  printf("\n[repK2 - underflow regression (Bug 1)]\n");

  // Construct shares manually for f(x) = secret + r*x mod M
  // with r chosen so that f(xj) mod M < f(xi) mod M  (i.e. wrap-around in yj).
  const uint64_t M = (uint64_t)FIELD_MODULUS;

  // r = M-1 is maximum, guarantees yj wraps aggressively for larger x.
  // f(1) = (secret + M-1) mod M = secret-1 (mod M)
  // f(2) = (secret + 2*(M-1)) mod M = (secret - 2 + 2M) mod M = secret - 2 + M (mod M) = secret-2
  // So yi = secret-1 (mod M), yj = secret-2 (mod M).
  // yi * xj = (secret-1)*2,  yj * xi = (secret-2)*1 → yi*xj > yj*xi, no underflow yet.

  // Try r = M/2 + 1 ≈ 4259841, xi=1, xj=3:
  // f(1) = secret + r  (mod M)
  // f(3) = secret + 3r (mod M) — 3r may wrap
  // For secret=1, r=4259841: f(1)=4259842, f(3)=(1+3*4259841)%M=(1+12779523)%M=12779524%M
  // 12779524 - M = 12779524 - 8519681 = 4259843
  // yi=4259842, yj=4259843, xi=1, xj=3
  // yi*xj = 4259842*3 = 12779526
  // yj*xi = 4259843*1 = 4259843
  // yi*xj > yj*xi → no underflow.

  // Need case where yj > yi AND yj*xi > yi*xj.
  // Let xi=1, xj=2. yi*xj = 2*yi, yj*xi = yj. Underflow when yj > 2*yi.
  // f(1)=secret+r mod M, f(2)=secret+2r mod M.
  // Want (secret+2r mod M) > 2*(secret+r mod M).
  // Pick r = (3*M/4) = 6389760, secret=1:
  // f(1) = 1+6389760 = 6389761
  // f(2) = 1+2*6389760 = 1+12779520 = 12779521 → 12779521-M=4259840
  // yi=6389761, yj=4259840, xi=1, xj=2
  // yi*xj = 6389761*2 = 12779522
  // yj*xi = 4259840*1 = 4259840
  // Still yi*xj > yj*xi.

  // xi=2, xj=3, same r=6389760, secret=1:
  // yi = f(2) = 4259840
  // yj = f(3) = 1+3*6389760 mod M = 19169281 mod M = 19169281-2*M=19169281-17039362=2129919
  // yi*xj = 4259840*3 = 12779520
  // yj*xi = 2129919*2 = 4259838
  // yi*xj > yj*xi → no underflow.

  // xi=1, xj=4, r=6389760, secret=1:
  // yi = f(1) = 6389761
  // yj = f(4) = 1+4*6389760 mod M = 25559041 mod M = 25559041-3*M=25559041-25559043=-2...
  // 25559041-3*8519681=25559041-25559043=-2 → wrap: 25559041 mod 8519681:
  // 25559041/8519681=2.999..., 3*8519681=25559043 > 25559041 → q=2: 25559041-2*8519681=25559041-17039362=8519679
  // yj=8519679
  // yi*xj=6389761*4=25559044
  // yj*xi=8519679*1=8519679
  // yi*xj > yj*xi. Still no underflow.

  // Let me try a direct approach: pick yi and yj directly such that yj*xi > yi*xj.
  // xi=1, xj=2: need yj > 2*yi.
  // Set yi=1 (small), yj=M-1 (large). Then yj*xi=(M-1) > 2=yi*xj. UNDERFLOW!
  // But are these valid k=2 shares at x=1 and x=2?
  // f(1)=1, f(2)=M-1. Solve: secret+r*1=1 → r=1-secret mod M
  //                          secret+r*2=M-1 → secret+2-2*secret=M-1 → 2-secret=M-1 mod M
  //                          → secret=3-M mod M = 3 (mod M)
  // Check: secret=3, r=1-3=-2 mod M = M-2=8519679
  // f(1)=3+(M-2)=M+1 mod M=1 ✓
  // f(2)=3+2*(M-2)=3+2M-4=2M-1 mod M=M-1 ✓
  // Valid shares! secret=3, xi=1, xj=2, yi=1, yj=M-1.

  {
    uint64_t M2 = M;
    uint64_t xi=1, xj=2, yi=1, yj=M2-1;
    // yj*xi = M-1, yi*xj = 2 → yj*xi > yi*xj → underflow in old code
    int rec = repK2(xi, xj, yi, yj);
    check(rec == 3, "underflow case: secret=3 shares yi=1 yj=M-1");
  }

  // Another underflow case: xi=2, xj=5, yi=10, yj=M-3
  // Are these valid shares? secret+r*2=10, secret+r*5=M-3
  // 3r = M-3-10 = M-13 → r=(M-13)/3... need M-13 divisible by 3.
  // M=8519681, M-13=8519668, 8519668/3=2839889.3... not integer. Skip exact check, just test arithmetic.
  // The fix must work for arbitrary field elements, not just valid shares.
  {
    uint64_t M2 = M;
    uint64_t xi=1, xj=3, yi=2, yj=M2-2;
    // yj*xi = M-2, yi*xj = 6 → yj*xi > yi*xj → underflow in old code
    // Reconstruct what secret this corresponds to:
    // num = (2*3 - (M-2)*1) mod M = (6 - M+2) mod M = (8-M) mod M = 8 (since 8-M is negative, +M gives 8)
    // denom_inv = modinv(2, M)
    // secret = 8 * modinv(2, M) mod M = 4 (since M is odd, 2 has inverse (M+1)/2)
    uint64_t expected_inv2 = (M + 1) / 2; // modinv(2, M) when M is odd prime
    uint64_t expected = 8 * expected_inv2 % M;
    int rec = repK2(xi, xj, yi, yj);
    check((uint64_t)rec == expected, "underflow case: xi=1 xj=3 yi=2 yj=M-2");
  }
}

// ── Test: repK2 when secret*(xj-xi) >= FIELD_MODULUS (Bug 2) ────────────────

static void test_repK2_integer_division_regression()
{
  printf("\n[repK2 - integer division regression (Bug 2)]\n");

  // Build real k=2 shares from createSecretShares for secrets that trigger overflow.
  const long M = FIELD_MODULUS;
  initializeSharePoly(M);

  // secret=4300000, N=3: secret*(xj-xi) with xj-xi=2 → 8600000 > M=8519681
  // Old code: (8600000 % M) / 2 = 80319 / 2 = 40159 ≠ 4300000
  {
    int secret = 4300000;
    unsigned long *shares = createSecretShares(secret, 3, 2, 0);
    // Use pair (xi=1, xj=3), xj-xi=2
    int rec = repK2(1, 3, shares[0], shares[2]);
    free(shares);
    check(rec == secret, "secret=4300000 pair(1,3) xj-xi=2 overflow");
  }

  // secret=4300000, pair(1,3) again but also check pair(2,3) and pair(1,2)
  {
    int secret = 4300000;
    unsigned long *shares = createSecretShares(secret, 4, 2, 0);
    bool ok = true;
    for (int i = 0; i < 3; i++)
      for (int j = i+1; j < 4; j++) {
        int rec = repK2(i+1, j+1, shares[i], shares[j]);
        if (rec != secret) ok = false;
      }
    free(shares);
    check(ok, "secret=4300000 N=4 all pairs");
  }

  // secret=M-1=8519680: secret*1=M-1 < M, but secret*2=2M-2>M
  {
    int secret = (int)(FIELD_MODULUS - 1);
    unsigned long *shares = createSecretShares(secret, 4, 2, 0);
    bool ok = true;
    for (int i = 0; i < 3; i++)
      for (int j = i+1; j < 4; j++) {
        int rec = repK2(i+1, j+1, shares[i], shares[j]);
        if (rec != secret) ok = false;
      }
    free(shares);
    check(ok, "secret=M-1 N=4 all pairs");
  }

  // secret=M/2=4259840: secret*2=M-1<M (just under), secret*3=12779520>M
  {
    int secret = (int)(FIELD_MODULUS / 2);
    unsigned long *shares = createSecretShares(secret, 4, 2, 0);
    bool ok = true;
    for (int i = 0; i < 3; i++)
      for (int j = i+1; j < 4; j++) {
        int rec = repK2(i+1, j+1, shares[i], shares[j]);
        if (rec != secret) ok = false;
      }
    free(shares);
    check(ok, "secret=M/2 N=4 all pairs");
  }
}

// ── Test: round-trip with random secrets and N up to 16 ──────────────────────

static void test_repK2_roundtrip_random()
{
  printf("\n[repK2 - random round-trip]\n");

  const long M = FIELD_MODULUS;
  initializeSharePoly(M);
  srand(0xdeadbeef);

  bool all_ok = true;
  for (int trial = 0; trial < 200; trial++) {
    int secret = rand() % (int)M;
    int N = 2 + rand() % 15;  // N in [2,16]
    unsigned long *shares = createSecretShares(secret, N, 2, 0);
    for (int i = 0; i < N - 1; i++) {
      for (int j = i + 1; j < N; j++) {
        int rec = repK2(i+1, j+1, shares[i], shares[j]);
        if (rec != secret % (int)M) {
          printf("    FAIL trial=%d secret=%d N=%d pair(%d,%d) got=%d\n",
                 trial, secret, N, i+1, j+1, rec);
          all_ok = false;
        }
      }
    }
    free(shares);
  }
  check(all_ok, "200 random (secret, N) round-trips");
}

// ── Test: token=0 still works (regression guard for existing smoke test) ──────

static void test_repK2_token_zero()
{
  printf("\n[repK2 - token=0 regression]\n");

  const long M = FIELD_MODULUS;
  initializeSharePoly(M);

  unsigned long *shares = createSecretShares(0, 8, 2, 0);
  bool all_ok = true;
  for (int i = 0; i < 7; i++)
    for (int j = i+1; j < 8; j++) {
      int rec = repK2(i+1, j+1, shares[i], shares[j]);
      if (rec != 0) all_ok = false;
    }
  free(shares);
  check(all_ok, "secret=0 (token) all pairs reconstruct 0");
}

// ── Test: field_mod_inverse correctness ──────────────────────────────────────

static void test_field_mod_inverse_basic()
{
  printf("\n[field_mod_inverse - basic cases]\n");

  const long M = FIELD_MODULUS;

  // Test: field_mod_inverse(1) == 1
  {
    uint64_t inv = field_mod_inverse(1);
    check(inv == 1, "field_mod_inverse(1) == 1");
  }

  // Test: field_mod_inverse(M-1) round-trips
  // (M-1) * (M-1) ≡ 1 (mod M) because (M-1)^2 = M^2 - 2M + 1 ≡ 1 (mod M)
  {
    uint64_t a = M - 1;
    uint64_t inv = field_mod_inverse(a);
    uint64_t product = (a * inv) % M;
    check(product == 1, "field_mod_inverse(M-1) round-trip: a*inv ≡ 1 (mod M)");
  }

  // Test: For random values in [1, M), verify x * field_mod_inverse(x) ≡ 1 (mod M)
  {
    srand(0x12345678);
    bool all_ok = true;
    for (int trial = 0; trial < 50; trial++) {
      uint64_t x = 1 + (rand() % (M - 1));  // x in [1, M)
      uint64_t inv = field_mod_inverse(x);
      uint64_t product = (x * inv) % M;
      if (product != 1) {
        printf("    trial=%d x=%llu inv=%llu product=%llu (want 1)\n", trial,
               (unsigned long long)x, (unsigned long long)inv, (unsigned long long)product);
        all_ok = false;
      }
    }
    check(all_ok, "50 random x: field_mod_inverse(x) * x ≡ 1 (mod M)");
  }
}

// ── Test: field_mod_inverse edge cases ───────────────────────────────────────

static void test_field_mod_inverse_edge_cases()
{
  printf("\n[field_mod_inverse - edge cases]\n");

  const long M = FIELD_MODULUS;

  // Test: field_mod_inverse(2)
  // 2 * inv ≡ 1 (mod M) → inv = (M+1)/2 = 4259841
  {
    uint64_t inv = field_mod_inverse(2);
    uint64_t product = (2 * inv) % M;
    check(product == 1, "field_mod_inverse(2): 2*inv ≡ 1 (mod M)");
  }

  // Test: field_mod_inverse of small primes within [1, M)
  {
    uint64_t primes[] = {3, 5, 7, 11, 13, 17, 19, 23};
    bool all_ok = true;
    for (auto p : primes) {
      uint64_t inv = field_mod_inverse(p);
      uint64_t product = (p * inv) % M;
      if (product != 1) {
        printf("    p=%llu inv=%llu product=%llu (want 1)\n",
               (unsigned long long)p, (unsigned long long)inv, (unsigned long long)product);
        all_ok = false;
      }
    }
    check(all_ok, "field_mod_inverse of primes {3,5,7,11,13,17,19,23}");
  }

  // Test: field_mod_inverse(M/2) where M = 8519681
  // M/2 = 4259840 (integer division), so (M+1)/2 = 4259841
  // Verify (M/2) * inv ≡ 1 (mod M)
  {
    uint64_t a = M / 2;
    uint64_t inv = field_mod_inverse(a);
    uint64_t product = (a * inv) % M;
    check(product == 1, "field_mod_inverse(M/2): a*inv ≡ 1 (mod M)");
  }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
  // repK2's smallDiffInverse cache is sized by inN at first call; set it to the
  // production value so this test exercises the cached path (not the fallback).
  inN = 64;
  printf("=== Shamir k=2 unit tests ===\n");
  printf("FIELD_MODULUS = %ld\n", FIELD_MODULUS);

  test_createSecretShares_basic();
  test_repK2_underflow_regression();
  test_repK2_integer_division_regression();
  test_repK2_roundtrip_random();
  test_repK2_token_zero();
  test_field_mod_inverse_basic();
  test_field_mod_inverse_edge_cases();

  printf("\n%d passed, %d failed\n", g_pass, g_fail);
  return g_fail > 0 ? 1 : 0;
}
