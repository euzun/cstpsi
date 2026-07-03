// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef PARAM_LOADER_H
#define PARAM_LOADER_H

#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class ParamConfig {
public:
    // Load from JSON file
    void loadFromJSON(const std::string& json_path);

    // Apply to global parameters (extern variables in params.h)
    void applyToGlobals();

    // Initialize context - calls initializePolynomial() and the SEAL setup chain.
    void initializeContext();

    // Getters
    std::string getName() const { return name_; }
    std::string getDatasetPath() const { return dataset_path_; }
    std::string getDatasetFormat() const { return dataset_format_; }
    std::string getEnrollmentPath() const { return enrollment_path_; }
    std::string getQueryPath() const { return query_path_; }
    std::string getOutputPath() const { return output_path_; }
    int getEnrBits() const { return enr_bits_; }
    int getEnrTotal() const { return enr_total_; }  // Actual enrollment count in file
    int getPolyModulusDegree() const { return poly_modulus_degree_; }

    // Validation
    bool validate() const;
    void printSummary() const;

private:
    // Protocol parameters (configurable)
    int N_;

    // Database parameters
    int nrof_que_ids_;
    int enr_bits_;   // Used to compute nrof_enr_ids = 2^enr_bits (protocol parameter)
    int enr_total_;  // Actual enrollment count in file (may differ from 2^enr_bits)

    // Performance parameters
    int m_, partition_size_, nrof_splits_;
    int nrof_collisions_;
    int nrof_online_threads_;
    int nrof_offline_threads_;

    // SEAL parameters
    int poly_modulus_degree_;
    uint64_t plain_modulus_;  // This is FIELD_MODULUS

    // Dataset paths
    std::string dataset_format_;  // "binary" or "csv"
    std::string dataset_path_;    // For binary format
    std::string enrollment_path_; // For CSV format
    std::string query_path_;      // For CSV format
    std::string output_path_;     // For CSV format

    // Metadata
    std::string name_;
    std::string description_;
};

#endif
