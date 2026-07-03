// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Unit tests for AES output reduction.
 *
 * Covers:
 *   - Zero-avoidance: all-zero output returns 1
 *   - FIPS-197 test vector: known AES output reduces deterministically
 *   - All-0xFF output: reduction modulo field_modulus
 *   - Collision resistance: different outputs produce different results
 */

#include "aes_key.h"
#include "params.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace cstpsi::gc;

static int g_pass = 0;
static int g_fail = 0;

static void test_assert(int condition, const char* test_name) {
    if (condition) {
        g_pass++;
        printf("  PASS: %s\n", test_name);
    } else {
        g_fail++;
        printf("  FAIL: %s\n", test_name);
    }
}

static void test_zero_avoidance() {
    printf("\n=== Zero-Avoidance Test ===\n");

    uint8_t output[16] = {0};
    uint64_t result = reduceAesOutput(output, FIELD_MODULUS);
    test_assert(result == 1, "All-zero output returns 1");
}

static void test_fips197_vector() {
    printf("\n=== FIPS-197 Test Vector ===\n");

    // AES-128 FIPS-197 test vector:
    // Key: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
    // Plaintext: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
    // Expected ciphertext: 69 c4 e0 d8 6a 7b 04 30 d8 a8 be d1 30 ac 59 62
    // (This is the standard FIPS-197 test vector, Appendix C.1)

    uint8_t expected_output[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xa8, 0xbe, 0xd1, 0x30, 0xac, 0x59, 0x62
    };

    uint64_t result = reduceAesOutput(expected_output, FIELD_MODULUS);

    // Verify:
    // - Result is not zero (should be non-zero for any non-zero input)
    test_assert(result != 0, "FIPS-197 output reduces to non-zero");

    // - Reduction is deterministic (call twice, should be same)
    uint64_t result2 = reduceAesOutput(expected_output, FIELD_MODULUS);
    test_assert(result == result2, "FIPS-197 reduction is deterministic");

    // - Result is less than field_modulus
    test_assert(result < FIELD_MODULUS, "FIPS-197 result < field_modulus");

    printf("    FIPS-197 output reduces to: %lu (mod %lu)\n", result, FIELD_MODULUS);
}

static void test_all_ones() {
    printf("\n=== All-0xFF Test ===\n");

    uint8_t output[16];
    memset(output, 0xFF, 16);

    uint64_t result = reduceAesOutput(output, FIELD_MODULUS);

    // Verify:
    // - Result is non-zero
    test_assert(result != 0, "All-0xFF output reduces to non-zero");

    // - Result is less than field_modulus
    test_assert(result < FIELD_MODULUS, "All-0xFF result < field_modulus");

    printf("    0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF reduces to: %lu (mod %lu)\n",
           result, FIELD_MODULUS);
}

static void test_collision_resistance() {
    printf("\n=== Collision Resistance Test ===\n");

    uint8_t output1[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xa8, 0xbe, 0xd1, 0x30, 0xac, 0x59, 0x62
    };

    uint8_t output2[16] = {
        0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
        0xd8, 0xa8, 0xbe, 0xd1, 0x30, 0xac, 0x59, 0x63
    };

    uint64_t result1 = reduceAesOutput(output1, FIELD_MODULUS);
    uint64_t result2 = reduceAesOutput(output2, FIELD_MODULUS);

    test_assert(result1 != result2, "Different outputs produce different results");

    printf("    Output 1 -> %lu, Output 2 -> %lu (both mod %lu)\n",
           result1, result2, FIELD_MODULUS);
}

int main(int argc, char* argv[]) {
    printf("=== AES Output Reduction Unit Tests ===\n");
    printf("Field modulus: %lu\n\n", FIELD_MODULUS);

    test_zero_avoidance();
    test_fips197_vector();
    test_all_ones();
    test_collision_resistance();

    printf("\n=== Summary ===\n");
    printf("Passed: %d\n", g_pass);
    printf("Failed: %d\n", g_fail);

    return (g_fail == 0) ? 0 : 1;
}
