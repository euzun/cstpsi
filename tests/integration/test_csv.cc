// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include <iostream>
#include <cassert>
#include <fstream>
#include <sstream>
#include <cmath>
#include <vector>
#include "csv.h"

// Mock globals satisfying the externs in src/protocol/params.h.
// FLPSI-era globals (inML, hash_size, xl_shift, xr_shift, nrof_id_bytes,
// nrof_lsh_bytes, nrof_share_bytes, nrof_id_shares, nrof_batch_que,
// nrof_enr_bits, mask_batch_size, nrof_field_mod_bits, distinct_que_id_checks)
// were removed in 2247a9e and don't need stubs anymore.
int nrof_que_ids = 0;
int inN = 4;   // small N for tests
int m = 2048;
int partition_size = 16;
int nrof_splits = 1;
int nrof_collisions = 1;
int nrof_enr_ids = 0;
int nrof_enr_total = 0;
uint64_t MAX_SUB = 0;
int inK = 2;
long FIELD_MODULUS = 8519681;
int nrof_online_threads = 4;

extern "C" {
    void initializePolynomial(long pol_modulus) {}

    void initializeSharePoly(long _pol_modulus) {}

    unsigned long *createSecretShares(int secret, int N, int K, int tid) {
        unsigned long* shares = new unsigned long[N];
        for (int i = 0; i < N; i++) {
            shares[i] = secret + i;
        }
        return shares;
    }
}

// ============================================================================
// Test Helper Functions
// ============================================================================

std::string createTempCSV(const std::string& filename, const std::vector<std::string>& lines) {
    std::string path = "/tmp/" + filename;
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create temp file: " + path);
    }
    for (const auto& line : lines) {
        file << line << "\n";
    }
    file.close();
    return path;
}

void removeTempFile(const std::string& path) {
    std::remove(path.c_str());
}

// ============================================================================
// Tests for integer-set CSV format
// ============================================================================

void test_load_enrollment_basic() {
    std::cout << "Test 1: Load enrollment CSV (basic, N=4)... ";

    std::vector<std::string> lines = {
        "id,item0,item1,item2,item3",
        "0,100,200,300,400",
        "1,500,600,700,800"
    };

    std::string csv_path = createTempCSV("test_enr_basic.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;
        CSVDataLoader::loadEnrollmentCSV(csv_path, enr_ss_map, token_share_map, id_share_map);

        assert(enr_ss_map.size() == 2);
        assert(enr_ss_map.find(0) != enr_ss_map.end());
        assert(enr_ss_map.find(1) != enr_ss_map.end());
        assert(enr_ss_map[0].size() == 4);
        assert(enr_ss_map[0][0] == 100);
        assert(enr_ss_map[0][1] == 200);
        assert(enr_ss_map[1][0] == 500);
        assert(token_share_map.size() == 2);
        assert(id_share_map.size() == 2);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_enrollment_skip_header() {
    std::cout << "Test 2: Load enrollment CSV (skip header)... ";

    std::vector<std::string> lines = {
        "id,item0,item1,item2,item3",
        "5,11,22,33,44"
    };

    std::string csv_path = createTempCSV("test_enr_header.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;
        CSVDataLoader::loadEnrollmentCSV(csv_path, enr_ss_map, token_share_map, id_share_map);

        assert(enr_ss_map.size() == 1);
        assert(enr_ss_map[5][0] == 11);
        assert(enr_ss_map[5][3] == 44);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_enrollment_missing_columns() {
    std::cout << "Test 3: Load enrollment CSV (missing columns skipped)... ";

    std::vector<std::string> lines = {
        "0",   // only ID, missing items — skipped
        "1,10,20,30,40"
    };

    std::string csv_path = createTempCSV("test_enr_missing.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;
        CSVDataLoader::loadEnrollmentCSV(csv_path, enr_ss_map, token_share_map, id_share_map);

        assert(enr_ss_map.size() == 1);
        assert(enr_ss_map.find(0) == enr_ss_map.end());
        assert(enr_ss_map.find(1) != enr_ss_map.end());

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_query_basic() {
    std::cout << "Test 4: Load query CSV (basic)... ";

    std::vector<std::string> lines = {
        "0,100,200,300,400",
        "1,500,600,700,800"
    };

    std::string csv_path = createTempCSV("test_que_basic.csv", lines);

    try {
        I2SSM que_ss_map;
        CSVDataLoader::loadQueryCSV(csv_path, que_ss_map);

        assert(que_ss_map.size() == 2);
        assert(que_ss_map[0][2] == 300);
        assert(que_ss_map[1][3] == 800);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_write_results() {
    std::cout << "Test 5: Write results CSV... ";

    std::string csv_path = "/tmp/test_results.csv";

    std::vector<std::vector<int>> matching_ids = {
        {3, 7},
        {},
        {5}
    };

    try {
        CSVDataLoader::writeResultsCSV(csv_path, matching_ids);

        std::ifstream file(csv_path);
        assert(file.is_open());

        std::string line;
        std::getline(file, line);  // header
        assert(line == "query_id,match_count,matched_ids");

        std::getline(file, line);  // first row: 0,2,"3 7"
        assert(line.find("0,2,") != std::string::npos);

        std::getline(file, line);  // second row: 1,0,""
        assert(line.find("1,0,") != std::string::npos);

        file.close();
        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_enrollment_large_item_values() {
    std::cout << "Test 6: Load enrollment CSV (large uint64 values)... ";

    std::vector<std::string> lines = {
        "0,8519680,8519679,1,2"   // values near FIELD_MODULUS
    };

    std::string csv_path = createTempCSV("test_enr_large.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;
        CSVDataLoader::loadEnrollmentCSV(csv_path, enr_ss_map, token_share_map, id_share_map);

        assert(enr_ss_map.size() == 1);
        assert(enr_ss_map[0][0] == 8519680);
        assert(enr_ss_map[0][2] == 1);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_empty_csv() {
    std::cout << "Test 7: Load empty CSV (no data rows)... ";

    std::vector<std::string> lines = {
        "# comment line",
        "",
        "id,item0,item1,item2,item3"
    };

    std::string csv_path = createTempCSV("test_empty.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;
        CSVDataLoader::loadEnrollmentCSV(csv_path, enr_ss_map, token_share_map, id_share_map);

        assert(enr_ss_map.size() == 0);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_query_with_is_tp_column() {
    std::cout << "Test 8: Load query CSV with is_tp column (passthrough)... ";

    std::vector<std::string> lines = {
        "0,100,200,300,400,1",  // trailing is_tp=1
        "1,500,600,700,800,0"   // trailing is_tp=0
    };

    std::string csv_path = createTempCSV("test_que_is_tp.csv", lines);

    try {
        I2SSM que_ss_map;
        CSVDataLoader::loadQueryCSV(csv_path, que_ss_map);

        // loadQueryCSV should parse all rows (not drop those with is_tp column)
        assert(que_ss_map.size() == 2);
        // Items should be read correctly (columns 1-4, ignoring column 5 which is is_tp)
        assert(que_ss_map[0][0] == 100);
        assert(que_ss_map[0][3] == 400);
        assert(que_ss_map[1][0] == 500);
        assert(que_ss_map[1][3] == 800);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

void test_load_enrollment_multi_label_columns() {
    std::cout << "Test 9: Load enrollment with multi-column labels... ";

    std::vector<std::string> lines = {
        "id,item0,item1,item2,item3,label0,label1,label2",
        "0,100,200,300,400,1001,1002,1003",
        "1,500,600,700,800,2001,2002,2003"
    };

    std::string csv_path = createTempCSV("test_enr_multilabel.csv", lines);

    try {
        I2SSM enr_ss_map, token_share_map, id_share_map;

        // Load with label_col_index = 5 (label0)
        CSVDataLoader::loadIdSharesOnly(csv_path, id_share_map, 5);
        assert(id_share_map.size() == 2);
        assert(id_share_map.count(0) > 0);
        assert(id_share_map.count(1) > 0);

        // Load with label_col_index = 6 (label1)
        I2SSM id_share_map2;
        CSVDataLoader::loadIdSharesOnly(csv_path, id_share_map2, 6);
        assert(id_share_map2.size() == 2);

        // Load with label_col_index = 7 (label2)
        I2SSM id_share_map3;
        CSVDataLoader::loadIdSharesOnly(csv_path, id_share_map3, 7);
        assert(id_share_map3.size() == 2);

        // Verify they have distinct share values (due to different secret values)
        // id_share_map[0] uses secret=1001, id_share_map2[0] uses secret=2001, etc.
        // The share at index 0 should differ
        // All three label columns must yield pairwise-distinct shares (require
        // ALL pairs to differ, so a collapse of any two columns is caught).
        bool distinct = (id_share_map[0][0]  != id_share_map2[0][0]) &&
                        (id_share_map[0][0]  != id_share_map3[0][0]) &&
                        (id_share_map2[0][0] != id_share_map3[0][0]);
        assert(distinct);

        removeTempFile(csv_path);
        std::cout << "PASSED" << std::endl;
    } catch (const std::exception& e) {
        removeTempFile(csv_path);
        std::cerr << "FAILED: " << e.what() << std::endl;
        throw;
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=== CSTPSI CSV Integration Tests (integer-set format) ===" << std::endl;
    std::cout << "N=" << inN << " items per set" << std::endl << std::endl;

    int passed = 0, failed = 0;

    auto run_test = [&](void (*test_fn)(), const char* name) {
        try {
            test_fn();
            passed++;
        } catch (const std::exception& e) {
            std::cerr << "  ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    run_test(test_load_enrollment_basic, "load_enrollment_basic");
    run_test(test_load_enrollment_skip_header, "load_enrollment_skip_header");
    run_test(test_load_enrollment_missing_columns, "load_enrollment_missing_columns");
    run_test(test_load_query_basic, "load_query_basic");
    run_test(test_write_results, "write_results");
    run_test(test_load_enrollment_large_item_values, "load_enrollment_large_item_values");
    run_test(test_load_empty_csv, "load_empty_csv");
    run_test(test_load_query_with_is_tp_column, "load_query_with_is_tp_column");
    run_test(test_load_enrollment_multi_label_columns, "load_enrollment_multi_label_columns");

    std::cout << std::endl;
    std::cout << "Results: " << passed << " passed, " << failed << " failed" << std::endl;

    return (failed > 0) ? 1 : 0;
}
