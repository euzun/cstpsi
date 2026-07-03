// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#ifndef CSTPSI_CSV
#define CSTPSI_CSV

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "helper.h"

extern "C" {
#include "modpoly.h"  // For createSecretShares
}

/**
 * CSV Data Loader for CSTPSI integer-set protocol.
 *
 * Enrollment/Query CSV format: id,item0,item1,...,itemN-1
 *   - id: integer enrollment/query ID
 *   - item0..itemN-1: uint64_t integer items (the N-item set)
 *
 * Results format: query_id,match_count,matched_ids
 */

class CSVDataLoader {
public:
    /**
     * Load enrollment database from CSV.
     * Reads id + N integer items per row; generates Shamir secret shares.
     * Format: id,item0,item1,...,itemN-1[,label]
     *
     * label_col_index: Column index for label value to use in id_share_map.
     *   -1 (default): Use row id as the secret (legacy behavior)
     *   >= 0: Use value at this column index as the secret
     */
    static void loadEnrollmentCSV(
        const std::string& csv_path,
        I2SSM& enr_ss_map,
        I2SSM& token_share_map,
        I2SSM& id_share_map,
        int label_col_index = -1
    );

    /**
     * Load query database from CSV.
     * Format: id,item0,item1,...,itemN-1
     */
    static void loadQueryCSV(
        const std::string& csv_path,
        I2SSM& que_ss_map
    );

    /**
     * Load only id_share_map for a given label column without touching
     * enr_ss_map or token_share_map.  Used by the multi-round sender to
     * compute per-round coefficient sets after the DB has been blinded.
     */
    static void loadIdSharesOnly(
        const std::string& csv_path,
        I2SSM& id_share_map,
        int label_col_index
    );

    /**
     * Write results to CSV.
     * Format: query_id,match_count,matched_ids
     */
    static void writeResultsCSV(
        const std::string& csv_path,
        const std::vector<std::vector<int>>& matching_ids
    );

private:
    static std::vector<std::string> parseCSVLine(const std::string& line);
};

#endif
