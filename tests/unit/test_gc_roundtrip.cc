// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Round-trip test: gcSimulateLocal output must equal blindItemOffline output.
 *
 * Critical invariant: the GC circuit and the offline AES path must produce
 * identical results for the same item and key. Without EMP, gcSimulateLocal
 * falls back to blindItemOffline directly — so this test always passes and
 * serves as a regression guard when EMP is later enabled.
 */

#include "aes_gc.h"
#include "params.h"
#include <cstdio>
#include <cstdlib>

using namespace cstpsi::gc;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool condition, const char* name) {
    if (condition) { g_pass++; printf("  PASS: %s\n", name); }
    else           { g_fail++; printf("  FAIL: %s\n", name); }
}

int main() {
    printf("=== GC Round-Trip Tests ===\n");

    AesKey key = generateAesKey();

    // Five fixed test items (≤23-bit, within FIELD_MODULUS)
    std::vector<uint64_t> items = {1, 42, 1000, 8519680, 999999};

    printf("\n--- gcSimulateLocal vs blindItemOffline ---\n");
    std::vector<uint64_t> gc_out = gcSimulateLocal(key, items, FIELD_MODULUS);

    for (size_t i = 0; i < items.size(); i++) {
        uint64_t offline = blindItemOffline(items[i], key, FIELD_MODULUS);
        char name[64];
        snprintf(name, sizeof(name), "item[%zu]=%llu: gc==offline", i, (unsigned long long)items[i]);
        check(gc_out[i] == offline, name);
    }

    printf("\n--- Zero-avoidance ---\n");
    for (size_t i = 0; i < gc_out.size(); i++) {
        char name[64];
        snprintf(name, sizeof(name), "gc_out[%zu] != 0", i);
        check(gc_out[i] != 0, name);
        check(gc_out[i] < FIELD_MODULUS, "gc_out < FIELD_MODULUS");
    }

    printf("\n--- Determinism: same item+key → same output ---\n");
    uint64_t a = blindItemOffline(12345, key, FIELD_MODULUS);
    uint64_t b = blindItemOffline(12345, key, FIELD_MODULUS);
    check(a == b, "blindItemOffline is deterministic");

    printf("\n--- Different keys → different outputs (with overwhelming probability) ---\n");
    AesKey key2 = generateAesKey();
    uint64_t c = blindItemOffline(12345, key,  FIELD_MODULUS);
    uint64_t d = blindItemOffline(12345, key2, FIELD_MODULUS);
    check(c != d, "different keys produce different outputs");

    printf("\n=== Summary ===\n");
    printf("Passed: %d / Failed: %d\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
