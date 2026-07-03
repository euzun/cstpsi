// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "csv.h"
#include <iostream>

std::vector<std::string> CSVDataLoader::parseCSVLine(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;

    while (std::getline(ss, token, ',')) {
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        token = (start != std::string::npos) ? token.substr(start, end - start + 1) : "";
        tokens.push_back(token);
    }

    return tokens;
}

void CSVDataLoader::loadEnrollmentCSV(
    const std::string& csv_path,
    I2SSM& enr_ss_map,
    I2SSM& token_share_map,
    I2SSM& id_share_map,
    int label_col_index
) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open enrollment CSV: " + csv_path);
    }

    initializeSharePoly(FIELD_MODULUS);

    std::string line;
    bool first_line = true;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> tokens = parseCSVLine(line);
        if (first_line && !tokens.empty() && tokens[0] == "id") {
            first_line = false;
            continue;
        }
        first_line = false;

        if (tokens.size() < (size_t)(1 + inN)) {
            std::cerr << "Warning: Line " << line_num << " has " << tokens.size()
                      << " columns, expected " << (1 + inN) << ", skipping" << std::endl;
            continue;
        }

        try {
            int id = std::stoi(tokens[0]);
            std::vector<uint64_t> items(inN);
            for (int j = 0; j < inN; j++) {
                items[j] = std::stoull(tokens[j + 1]);
            }
            enr_ss_map[id] = items;

            std::unique_ptr<unsigned long[]> token_shares(createSecretShares(0, inN, inK, 0));

            int share_secret = id;
            if (label_col_index >= 0 && (size_t)label_col_index < tokens.size()) {
                share_secret = static_cast<int>(std::stoull(tokens[label_col_index]));
            }
            std::unique_ptr<unsigned long[]> id_shares(createSecretShares(share_secret, inN, inK, 1));
            for (int i = 0; i < inN; i++) {
                token_share_map[id].push_back(token_shares[i]);
                id_share_map[id].push_back(id_shares[i]);
            }
        } catch (const std::exception& e) {
            std::cerr << "Warning: Line " << line_num << " failed: " << e.what() << ", skipping" << std::endl;
        }
    }

    file.close();
    std::cout << "Loaded " << enr_ss_map.size() << " enrollment entries from " << csv_path << std::endl;
}

void CSVDataLoader::loadQueryCSV(
    const std::string& csv_path,
    I2SSM& que_ss_map
) {
    std::ifstream file(csv_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open query CSV: " + csv_path);
    }

    std::string line;
    bool first_line = true;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> tokens = parseCSVLine(line);
        if (first_line && !tokens.empty() && tokens[0] == "id") {
            first_line = false;
            continue;
        }
        first_line = false;

        if (tokens.size() < (size_t)(1 + inN)) {
            std::cerr << "Warning: Line " << line_num << " has " << tokens.size()
                      << " columns, expected " << (1 + inN) << ", skipping" << std::endl;
            continue;
        }

        try {
            int id = std::stoi(tokens[0]);
            std::vector<uint64_t> items(inN);
            for (int j = 0; j < inN; j++) {
                items[j] = std::stoull(tokens[j + 1]);
            }
            que_ss_map[id] = items;
        } catch (const std::exception& e) {
            std::cerr << "Warning: Line " << line_num << " failed: " << e.what() << ", skipping" << std::endl;
        }
    }

    file.close();
    std::cout << "Loaded " << que_ss_map.size() << " query entries from " << csv_path << std::endl;
}

void CSVDataLoader::loadIdSharesOnly(
    const std::string& csv_path,
    I2SSM& id_share_map,
    int label_col_index
) {
    std::ifstream file(csv_path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open enrollment CSV: " + csv_path);

    initializeSharePoly(FIELD_MODULUS);
    id_share_map.clear();

    std::string line;
    bool first_line = true;
    int line_num = 0;

    while (std::getline(file, line)) {
        line_num++;
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> tokens = parseCSVLine(line);
        if (first_line && !tokens.empty() && tokens[0] == "id") {
            first_line = false;
            continue;
        }
        first_line = false;

        if (tokens.size() < (size_t)(1 + inN)) continue;

        try {
            int id = std::stoi(tokens[0]);
            int share_secret = id;
            if (label_col_index >= 0 && (size_t)label_col_index < tokens.size())
                share_secret = static_cast<int>(std::stoull(tokens[label_col_index]));

            std::unique_ptr<unsigned long[]> id_shares(
                createSecretShares(share_secret, inN, inK, 1)
            );
            id_share_map[id].clear();
            for (int i = 0; i < inN; i++)
                id_share_map[id].push_back(id_shares[i]);
        } catch (...) {}
    }
}

void CSVDataLoader::writeResultsCSV(
    const std::string& csv_path,
    const std::vector<std::vector<int>>& matching_ids
) {
    std::ofstream file(csv_path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open output CSV: " + csv_path);
    }

    file << "query_id,match_count,matched_ids" << std::endl;
    for (size_t i = 0; i < matching_ids.size(); i++) {
        file << i << "," << matching_ids[i].size() << ",\"";
        for (size_t j = 0; j < matching_ids[i].size(); j++) {
            if (j > 0) file << " ";
            file << matching_ids[i][j];
        }
        file << "\"" << std::endl;
    }

    file.close();
    std::cout << "Wrote " << matching_ids.size() << " results to " << csv_path << std::endl;
}
