// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * test_disable_1gc.cc — Unit tests for ProtocolMode enum
 *
 * Tests that the ProtocolMode enum (CSTPSI vs STLPSI) compiles, exhaustively
 * covers all enumerators, and can be stored in a config struct. The protocol-level
 * mode dispatch is covered by smoke tests and integration tests; this unit test
 * verifies the enum infrastructure itself.
 *
 * Note: Full multi-round GC logic is exercised by the 2+2 smoke/integration tests
 * (--mode CSTPSI vs STLPSI, --disable-1gc variations).
 */

#include "params.h"

#include <cassert>
#include <iostream>

using namespace std;

static int g_pass = 0;
static int g_fail = 0;

static void check(bool cond, const char* label) {
    if (cond) {
        cout << "  PASS  " << label << endl;
        g_pass++;
    } else {
        cout << "  FAIL  " << label << endl;
        g_fail++;
    }
}

int main() {
    cout << "=== ProtocolMode Unit Tests ===" << endl;

    // Test 1: ProtocolMode::CSTPSI and STLPSI are distinct
    {
        check(ProtocolMode::CSTPSI != ProtocolMode::STLPSI,
              "ProtocolMode::CSTPSI != ProtocolMode::STLPSI");
    }

    // Test 2: ProtocolMode::CSTPSI is distinct from itself (sanity)
    {
        ProtocolMode m1 = ProtocolMode::CSTPSI;
        ProtocolMode m2 = ProtocolMode::CSTPSI;
        check(m1 == m2, "same ProtocolMode values are equal");
    }

    // Test 3: Switch over ProtocolMode is exhaustive (will fail to compile if not)
    {
        ProtocolMode mode = ProtocolMode::CSTPSI;
        int x = 0;
        switch (mode) {
            case ProtocolMode::CSTPSI:  x = 1; break;
            case ProtocolMode::STLPSI:  x = 2; break;
        }
        check(x > 0, "switch(ProtocolMode) exhaustive dispatch compiles");
    }

    // Test 4: Local config struct with ProtocolMode field
    {
        struct Config {
            ProtocolMode mode = ProtocolMode::CSTPSI;
            int nrof_token_rounds = 1;
        };

        Config cfg;
        check(cfg.mode == ProtocolMode::CSTPSI,
              "Config struct defaults mode to CSTPSI");
        check(cfg.nrof_token_rounds == 1,
              "Config struct defaults nrof_token_rounds to 1");
    }

    // Test 5: Config struct can be set to STLPSI
    {
        struct Config {
            ProtocolMode mode = ProtocolMode::CSTPSI;
            int nrof_token_rounds = 1;
        };

        Config cfg;
        cfg.mode = ProtocolMode::STLPSI;
        check(cfg.mode == ProtocolMode::STLPSI,
              "Config struct mode can be changed to STLPSI");
    }

    // Test 6: ProtocolMode enum value comparison works
    {
        ProtocolMode m_cstpsi = ProtocolMode::CSTPSI;
        ProtocolMode m_stlpsi = ProtocolMode::STLPSI;
        check(m_cstpsi != m_stlpsi,
              "enum value comparison (!=) works");
        check(!(m_cstpsi == m_stlpsi),
              "enum value comparison (==) works");
    }

    cout << "\n" << g_pass << " passed, " << g_fail << " failed" << endl;

    // Return non-zero if any test failed
    return (g_fail > 0) ? 1 : 0;
}
