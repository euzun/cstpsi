// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "param_loader.h"
#include <iostream>
#include <cmath>

// Forward declarations for functions defined in other compilation units
extern "C" {
#include "modpoly.h"  // For initializePolynomial()
}

// Global variables (declared in params.h and defined in params.cc)
extern int nrof_que_ids;
extern int inN;
extern int m;
extern int partition_size;
extern int nrof_splits;
extern int nrof_collisions;
extern int nrof_enr_ids;
extern uint64_t MAX_SUB;
extern int inK;
extern long FIELD_MODULUS;
extern int nrof_online_threads;
extern int nrof_offline_threads;
extern int nrof_enr_total;

void ParamConfig::loadFromJSON(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open parameter file: " + json_path);
    }

    json j;
    file >> j;

    // Parse protocol params
    N_ = j["protocol_params"]["N"];

    // Parse database params
    nrof_que_ids_ = j["database_params"]["nrof_que_ids"];
    enr_bits_ = j["dataset"]["enr_bits"];  // Get from dataset section
    // enr_total: actual enrollment count in file (defaults to 2^enr_bits if not specified)
    if (j["dataset"].contains("enr_total")) {
        enr_total_ = j["dataset"]["enr_total"];
    } else {
        enr_total_ = (1 << enr_bits_);
    }

    // Parse performance params
    m_ = j["performance_params"]["m"];
    partition_size_ = j["performance_params"]["partition_size"];
    nrof_splits_ = j["performance_params"]["nrof_splits"];
    nrof_collisions_ = j["performance_params"]["nrof_collisions"];
    // Thread counts are OPTIONAL: default to (cores - 2) (the params.cc globals)
    // when absent from the config.
    nrof_online_threads_  = j["performance_params"].value("nrof_online_threads",  nrof_online_threads);
    nrof_offline_threads_ = j["performance_params"].value("nrof_offline_threads", nrof_offline_threads);

    // Parse SEAL params
    poly_modulus_degree_ = j["seal_params"]["poly_modulus_degree"];
    plain_modulus_ = j["seal_params"]["plain_modulus"];

    // Parse dataset
    dataset_format_ = j["dataset"]["format"];
    if (dataset_format_ == "binary") {
        dataset_path_ = j["dataset"]["path"];
    } else if (dataset_format_ == "csv") {
        enrollment_path_ = j["dataset"]["enrollment_path"];
        query_path_ = j["dataset"]["query_path"];
        output_path_ = j["dataset"]["output_path"];
    }

    // Parse metadata
    name_ = j["name"];
    description_ = j.value("description", std::string{});
}

void ParamConfig::applyToGlobals() {
    // Set configurable global parameters
    inN = N_;
    nrof_que_ids = nrof_que_ids_;
    m = m_;
    partition_size = partition_size_;
    nrof_splits = nrof_splits_;
    nrof_collisions = nrof_collisions_;
    nrof_online_threads = nrof_online_threads_;
    nrof_offline_threads = nrof_offline_threads_;

    // Hardcoded parameters (NOT from JSON)
    inK = 2;                // Always 2 (threshold)

    // Compute nrof_enr_ids from enr_bits (used for data loading)
    nrof_enr_ids = 1 << enr_bits_;
    nrof_enr_total = enr_total_;  // Actual file enrollment count (may differ from nrof_enr_ids)

    // Safe upper-bound for subsample values: FIELD_MODULUS - 1.  Callers that
    // care about the actual data-driven max (e.g. cli.cc synthetic bench)
    // overwrite MAX_SUB inline.
    MAX_SUB = FIELD_MODULUS - 1;

    // Set FIELD_MODULUS from plain_modulus
    FIELD_MODULUS = plain_modulus_;
}

void ParamConfig::initializeContext() {
    initializePolynomial(FIELD_MODULUS);  // Initialize FLINT polynomial (from modpoly.h)
}

bool ParamConfig::validate() const {
    bool valid = true;

    // Validate protocol params
    if (N_ < 1 || N_ > 256) {
        std::cerr << "Invalid N: " << N_ << " (must be 1-256)" << std::endl;
        valid = false;
    }

    // Validate SEAL params
    if (poly_modulus_degree_ != 2048 && poly_modulus_degree_ != 4096 &&
        poly_modulus_degree_ != 8192 && poly_modulus_degree_ != 16384) {
        std::cerr << "Invalid poly_modulus_degree: " << poly_modulus_degree_ << std::endl;
        valid = false;
    }

    // Validate enr_bits
    if (enr_bits_ < 1 || enr_bits_ > 24) {
        std::cerr << "Invalid enr_bits: " << enr_bits_ << " (must be 1-24)" << std::endl;
        valid = false;
    }

    return valid;
}

void ParamConfig::printSummary() const {
    std::cout << "\n=== Parameter Configuration: " << name_ << " ===" << std::endl;
    std::cout << description_ << std::endl;
    std::cout << "\nProtocol: N=" << N_ << ", k=2 (hardcoded)" << std::endl;
    std::cout << "Database: " << (1 << enr_bits_) << " enrollments (2^" << enr_bits_
              << "), " << nrof_que_ids_ << " queries" << std::endl;
    std::cout << "Performance: m=" << m_ << ", partition_size=" << partition_size_
              << ", online_threads=" << nrof_online_threads_
              << ", offline_threads=" << nrof_offline_threads_ << std::endl;
    std::cout << "SEAL: poly_degree=" << poly_modulus_degree_
              << ", plain_modulus=" << plain_modulus_ << std::endl;
    std::cout << "Dataset format: " << dataset_format_ << std::endl;
    std::cout << "======================================\n" << std::endl;
}
