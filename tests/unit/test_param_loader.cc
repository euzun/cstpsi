// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
// Unit tests for ParamConfig (src/io/param_loader.{h,cc}).
// Runs from repo root; resolves the fixture via the relative path
// "parameters/demo_1k.json".

#include <iostream>
#include <cassert>
#include <fstream>
#include <cstdio>
#include "param_loader.h"

// Mock globals — ParamConfig writes into these via extern. Limited to the
// set actually declared in src/protocol/params.h (FLPSI-era globals were
// removed in 2247a9e; their mocks are gone here too).
int nrof_que_ids = 0;
int inN = 0;
int m = 0;
int partition_size = 0;
int nrof_splits = 0;
int nrof_collisions = 0;
int nrof_enr_ids = 0;
int nrof_enr_total = 0;
uint64_t MAX_SUB = 0;
int inK = 0;
long FIELD_MODULUS = 0;
int nrof_online_threads = 0;
int nrof_offline_threads = 0;

// Stub: ParamConfig::initializeContext() calls this transitively via the
// SEAL setup path. Empty body is fine for unit-test scope.
extern "C" {
    void initializePolynomial(long /*pol_modulus*/) {}
}

static const char* DEMO_JSON = "parameters/demo_1k.json";

void test_json_loading() {
    std::cout << "Test 1: JSON loading... ";
    ParamConfig config;
    config.loadFromJSON(DEMO_JSON);
    assert(config.getName() == "demo_1k");
    assert(config.getDatasetFormat() == "csv");
    assert(config.getEnrBits() == 10);
    std::cout << "PASSED" << std::endl;
}

void test_config_validation() {
    std::cout << "Test 2: demo_1k validation... ";
    ParamConfig config;
    config.loadFromJSON(DEMO_JSON);
    assert(config.validate());
    std::cout << "PASSED" << std::endl;
}

void test_apply_globals() {
    std::cout << "Test 3: applyToGlobals writes the expected values... ";
    ParamConfig config;
    config.loadFromJSON(DEMO_JSON);
    config.applyToGlobals();

    assert(inN == 64);                 // from JSON
    assert(nrof_que_ids == 1200);      // from JSON
    assert(inK == 2);                  // hardcoded in param_loader.cc
    assert(nrof_enr_ids == (1 << 10)); // 2^enr_bits = 2^10 = 1024
    std::cout << "PASSED" << std::endl;
}

void test_initialize_context() {
    std::cout << "Test 4: initializeContext does not crash... ";
    ParamConfig config;
    config.loadFromJSON(DEMO_JSON);
    config.applyToGlobals();
    config.initializeContext();
    std::cout << "PASSED" << std::endl;
}

void test_getters() {
    std::cout << "Test 5: getters return JSON dataset paths verbatim... ";
    ParamConfig config;
    config.loadFromJSON(DEMO_JSON);
    assert(config.getDatasetFormat() == "csv");
    assert(config.getEnrollmentPath() == "demo/data/enr_1k_lbl23bit.csv");
    assert(config.getQueryPath() == "demo/data/qry_1k.csv");
    assert(config.getOutputPath() == "demo/results/demo_output.csv");
    assert(config.getEnrBits() == 10);
    std::cout << "PASSED" << std::endl;
}

void test_missing_file() {
    std::cout << "Test 6: missing file raises runtime_error... ";
    ParamConfig config;
    try {
        config.loadFromJSON("/nonexistent/path/params.json");
        assert(false);
    } catch (const std::runtime_error& e) {
        assert(std::string(e.what()).find("Cannot open") != std::string::npos);
    }
    std::cout << "PASSED" << std::endl;
}

void test_missing_description_key() {
    std::cout << "Test 7: JSON with missing 'description' key... ";

    // Create a temporary JSON file missing the "description" key
    std::string temp_json = "/tmp/test_missing_description.json";
    std::ofstream temp_file(temp_json);
    if (!temp_file.is_open()) {
        throw std::runtime_error("Cannot create temp JSON file");
    }

    // Write minimal JSON without "description" field
    temp_file << R"({
  "name": "test_no_desc",
  "protocol_params": {"N": 32},
  "database_params": {"nrof_que_ids": 10},
  "performance_params": {
    "m": 2048,
    "partition_size": 16,
    "nrof_splits": 1,
    "nrof_collisions": 1,
    "nrof_online_threads": 4
  },
  "seal_params": {
    "poly_modulus_degree": 4096,
    "plain_modulus": 8519681
  },
  "dataset": {
    "format": "csv",
    "enr_bits": 7,
    "enrollment_path": "data/enr.csv",
    "query_path": "data/que.csv",
    "output_path": "data/out.csv"
  }
})";
    temp_file.close();

    try {
        ParamConfig config;
        config.loadFromJSON(temp_json);
        // j.value("description", "") should return empty string when key is missing
        assert(config.getName() == "test_no_desc");
        // Description getter not exposed, but loadFromJSON should not throw
        std::cout << "PASSED" << std::endl;
        std::remove(temp_json.c_str());
    } catch (const std::exception& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
        std::remove(temp_json.c_str());
        throw;
    }
}

int main() {
    std::cout << "\n=== Parameter Loader Unit Tests ===" << std::endl;
    try {
        test_json_loading();
        test_config_validation();
        test_apply_globals();
        test_initialize_context();
        test_getters();
        test_missing_file();
        test_missing_description_key();
        std::cout << "\n=== All Tests PASSED ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED: " << e.what() << std::endl;
        return 1;
    }
}
