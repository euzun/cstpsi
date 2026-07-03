// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * CSTPSI CLI Tool
 *
 * Modes:
 *  bench         - Synthetic benchmark (no data file needed; FRR/FAR verification)
 *  csv_run       - Full PSI run from CSV integer-set files
 *  help          - Show usage
 *
 * Copyright 2026 - CSTPSI Publication-Ready Infrastructure
 */

#include <iostream>
#include <string>
#include <chrono>
#include <filesystem>
#include <uuid/uuid.h>
#include "sender.h"
#include "io.h"
#include "param_loader.h"
#include "csv.h"
#include "aes_gc.h"
#include "aes_key.h"
#include "instrumentation.h"

using namespace std;
using namespace std::chrono;
using namespace cstpsi::gc;
using namespace cstpsi::instrumentation;

namespace fs = std::filesystem;

// Forward declarations for functions from core.cc
extern vector<int> submitSingleQuery(SealCredentials &sc, vector<uint64_t> &que_subsamples,
                                     PVector3D &encoded_coeffs);

// Forward declarations for receiver-side functions used in the instrumented
// inline path (so we can time each sub-call separately).
extern UVector2D computeQueryPowers(vector<uint64_t> &que_subsamples);
extern UVector2D simdQueryPowers(UVector2D &query_powers);
extern vector<Ciphertext> encryptQueryPowers(SealCredentials &sc, UVector2D &simd_query_powers);
extern UVector3D decryptQueryResult(SealCredentials &sc, CVector2D &encrypted_result_token_id, int nrof_real_partitions = -1);
extern vector<int> findMatches(UVector3D &reformed_result_token_id);

// Sender-side: per-round homomorphic evaluation.
extern CVector2D homomorphicPolyEval(SealCredentials &sc,
                                     vector<Ciphertext> &encrypted_query_powers,
                                     PVector3D &encoded_coeffs);

// Global share maps defined in core.cc
extern I2SSM token_share_map;
extern I2SSM id_share_map;

struct CLIOptions {
    string mode;              // "bench", "csv_run", "help"
    string param_file;        // JSON parameter file
    string db_file;           // Enrollment CSV (override JSON)
    string query_file;        // Query CSV (override JSON)
    string output_file;       // Results output (override JSON)
    string output_jsonl;      // JSONL instrumentation output (optional)
    bool verbose = false;     // Verbose output
};

/**
 * Validate user-provided file paths to prevent directory traversal attacks
 */
static void validateFilePath(const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("File path cannot be empty");
    }
    if (path.find("..") != std::string::npos) {
        throw std::runtime_error("Path traversal (..) is not allowed: " + path);
    }
    if (fs::path(path).is_absolute()) {
        fs::path abs_path = fs::absolute(path);
        fs::path cwd = fs::current_path();
        try {
            fs::path rel = abs_path.lexically_relative(cwd);
            if (rel.string().find("..") != std::string::npos) {
                throw std::runtime_error("Absolute path is outside project directory: " + path);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error("Invalid absolute path: " + path);
        }
    }
}

void printUsage() {
    cout << "CSTPSI CLI - Set Threshold Labeled Private Set Intersection" << endl;
    cout << "============================================================" << endl;
    cout << endl;
    cout << "Usage: cstpsi_cli <mode> [options]" << endl;
    cout << endl;
    cout << "Modes:" << endl;
    cout << "  bench         Synthetic benchmark (no data file needed, FRR/FAR verification)" << endl;
    cout << "  csv_run       Full PSI run from CSV integer-set files" << endl;
    cout << "  help          Show this help message" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  --params <file>       JSON parameter configuration file (required)" << endl;
    cout << "  --db <file>           Enrollment CSV (overrides params JSON path)" << endl;
    cout << "  --queries <file>      Query CSV (overrides params JSON path)" << endl;
    cout << "  --output <file>       Output CSV (overrides params JSON path)" << endl;
    cout << "  --output-jsonl <file> JSONL instrumentation output (optional)" << endl;
    cout << "  --verbose             Verbose output" << endl;
    cout << endl;
    cout << "Examples:" << endl;
    cout << "  # Synthetic benchmark (no data files needed)" << endl;
    cout << "  cstpsi_cli bench --params experiments/configs/smoke_1k_N64_k2.json" << endl;
    cout << endl;
    cout << "  # Synthetic benchmark with instrumentation" << endl;
    cout << "  cstpsi_cli bench --params experiments/configs/smoke_1k_N64_k2.json --output-jsonl results.jsonl" << endl;
    cout << endl;
    cout << "  # Full run from CSV files" << endl;
    cout << "  cstpsi_cli csv_run --params experiments/configs/smoke_1k_N64_k2.json" << endl;
    cout << endl;
}

CLIOptions parseArgs(int argc, char* argv[]) {
    CLIOptions opts;

    if (argc < 2) {
        printUsage();
        exit(1);
    }

    opts.mode = argv[1];

    if (opts.mode == "help" || opts.mode == "--help" || opts.mode == "-h") {
        printUsage();
        exit(0);
    }

    for (int i = 2; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--params" && i + 1 < argc) {
            opts.param_file = argv[++i];
        } else if (arg == "--db" && i + 1 < argc) {
            opts.db_file = argv[++i];
        } else if (arg == "--queries" && i + 1 < argc) {
            opts.query_file = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            opts.output_file = argv[++i];
        } else if (arg == "--output-jsonl" && i + 1 < argc) {
            opts.output_jsonl = argv[++i];
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else {
            cerr << "Unknown option: " << arg << endl;
            printUsage();
            exit(1);
        }
    }

    if (opts.param_file.empty()) {
        cerr << "Error: --params is required" << endl;
        printUsage();
        exit(1);
    }

    return opts;
}

/**
 * Generate a UUID string for run_id.
 */
static string generateUUID() {
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_str[37];
    uuid_unparse(uuid, uuid_str);
    return string(uuid_str);
}

/**
 * Run a shell command and capture its stdout (single line, trimmed).
 * Returns empty string on failure.
 */
static string captureCommand(const string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    char buf[512] = {0};
    string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    // Trim trailing newline
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

/**
 * Detect hardware info (cpu, ram_gb, os, git_sha) into a json object.
 */
static json detectHardware() {
    json h;
    #ifdef __APPLE__
        string cpu = captureCommand("sysctl -n machdep.cpu.brand_string 2>/dev/null");
        string ram = captureCommand("sysctl -n hw.memsize 2>/dev/null");
        h["cpu"] = cpu.empty() ? "unknown" : cpu;
        h["ram_gb"] = ram.empty() ? 0 : (int)(std::stoll(ram) / (1024LL * 1024LL * 1024LL));
        string os_ver = captureCommand("sw_vers -productVersion 2>/dev/null");
        h["os"] = string("macOS ") + (os_ver.empty() ? "?" : os_ver);
    #else
        string cpu = captureCommand("lscpu | grep 'Model name' | sed 's/Model name:[[:space:]]*//' 2>/dev/null");
        string ram = captureCommand("grep MemTotal /proc/meminfo | awk '{print $2}' 2>/dev/null");
        h["cpu"] = cpu.empty() ? "unknown" : cpu;
        h["ram_gb"] = ram.empty() ? 0 : (int)(std::stoll(ram) / (1024LL * 1024LL));  // KB to GB
        string os_ver = captureCommand("uname -sr 2>/dev/null");
        h["os"] = os_ver.empty() ? "linux" : os_ver;
    #endif
    string sha = captureCommand("git rev-parse --short HEAD 2>/dev/null");
    h["git_sha"] = sha.empty() ? "unknown" : sha;
    return h;
}

/**
 * Detect software info (seal, emp, gcc) into a json object.
 */
static json detectSoftware() {
    json s;
    s["seal"] = "4.1.2";  // From dependency footnote in paper
    s["emp"] = "0.2.5/0.2.4/0.2.2";  // From CMakeLists pin (tool/ot/sh2pc)
    string gcc = captureCommand("c++ --version | head -1 2>/dev/null");
    s["gcc"] = gcc.empty() ? "unknown" : gcc;
    return s;
}

/**
 * CSTPSI synthetic benchmark: generates random integer sets, runs the full
 * PSI pipeline with true-positive queries, and verifies FRR=0%.
 *
 * Random subsamples in [1, FIELD_MODULUS) avoid:
 *  - FLINT modular arithmetic collisions (values stay < FM)
 *  - The linear-structure false-positive pathology (sequential values create
 *    non-matching partitions that also reconstruct token=0)
 * que_ss_map is populated AFTER parallelPartitionDB because isDistinct may
 * modify enr_ss_map in-place to resolve within-partition subsample collisions.
 */
void runStlpsiBench(const CLIOptions& opts) {
    cout << "\n=== CSTPSI Synthetic Benchmark ===" << endl;

    ParamConfig config;
    config.loadFromJSON(opts.param_file);
    config.printSummary();

    if (!config.validate()) {
        cerr << "Parameter validation failed" << endl;
        exit(1);
    }

    config.applyToGlobals();
    config.initializeContext();

    cout << "\nGenerating synthetic enrollment DB (" << nrof_enr_ids
         << " IDs, N=" << inN << " subsamples each)..." << endl;

    if (nrof_que_ids > nrof_enr_ids) {
        cerr << "Error: nrof_que_ids (" << nrof_que_ids
             << ") > nrof_enr_ids (" << nrof_enr_ids
             << "). Reduce nrof_que_ids in JSON config." << endl;
        exit(1);
    }

    // Initialize instrumentation if output requested
    Instrumentation inst;
    if (!opts.output_jsonl.empty()) {
        // Build config JSON manually from ParamConfig getters (no toJSON()).
        // Validator requires: D, N, k, T, K, label_bytes, threads, config_name (all int except config_name).
        json config_j;
        config_j["config_name"] = config.getName();
        config_j["D"] = nrof_enr_ids;     // database size (validator-required alias)
        config_j["N"] = inN;
        config_j["k"] = inK;               // threshold; hardcoded k=2 in cli mode
        config_j["T"] = 1;                 // token rounds; cli mode is T=1 (multi-token via --nrofTokenRounds in network mode)
        config_j["K"] = 1;                 // label chunks; cli mode is K=1
        config_j["label_bytes"] = 3;       // synthetic bench: 23-bit label ≈ 3 bytes (placeholder)
        config_j["threads"] = nrof_online_threads;
        // Extra context (not required by validator):
        config_j["dataset_format"] = config.getDatasetFormat();
        config_j["enr_bits"] = config.getEnrBits();
        config_j["enr_total"] = config.getEnrTotal();
        config_j["poly_modulus_degree"] = config.getPolyModulusDegree();
        config_j["nrof_que_ids"] = nrof_que_ids;
        inst.setConfig(config_j);
        inst.setSeeds(42, 1234);  // Fixed seeds for reproducibility
        inst.run_id = generateUUID();
        inst.setSystemInfo(detectHardware(), detectSoftware());
    }

    I2SSM enr_ss_map;
    MAX_SUB = 0;
    mt19937_64 rng(42); // fixed seed for reproducibility
    for (int pid = 0; pid < nrof_enr_ids; pid++) {
        vector<uint64_t> ss(inN);
        for (int j = 0; j < inN; j++) {
            uint64_t v = (rng() % (uint64_t)(FIELD_MODULUS - 1)) + 1;
            ss[j] = v;
            MAX_SUB = max(MAX_SUB, v);
        }
        enr_ss_map[pid] = ss;
    }

    // Note: initializeSharePoly + setShares loop is moved below into step_share bracket
    // so the offline timing actually captures the share construction.

    cout << "Synthetic data ready. Running PSI pipeline..." << endl;

    auto t_total_start = high_resolution_clock::now();
    bool instr = !opts.output_jsonl.empty();

    // STEP 4 (Share) happens earlier in this cli flow: the per-row Shamir
    // polynomials are sampled by setShares() during synthetic-data generation
    // above (lines 290-295 in source).  We re-bracket step_share here to
    // capture the (already-completed) share construction time conservatively
    // as part of the offline phase.  Sub-steps sample_coef_us / eval_shares_us
    // are left at 0 — separating them requires modifying core.cc::setShares
    // which is protocol code (out of scope for instrumentation pass).

    // ========== STEP 1: Init ==========
    AesKey gc_key;
    if (instr) inst.recordOfflineStepStart("step_init");
    gc_key = generateAesKey();
    if (instr) inst.recordOfflineStepEnd("step_init");

    // ========== STEP 2: Partition ==========
    if (instr) inst.recordOfflineStepStart("step_partition");
    I32Vector3D db_partitions = parallelPartitionDB(enr_ss_map);
    if (instr) inst.recordOfflineStepEnd("step_partition");

    vector<int> enr_keys;
    for (int i = 0; i < nrof_enr_ids; i++) {
        if (enr_ss_map.count(i)) enr_keys.push_back(i);
    }
    std::sort(enr_keys.begin(), enr_keys.end());

    // Copy true-positive queries AFTER partitioning (collision resolution may modify enr_ss_map)
    I2SSM que_ss_map;
    for (int i = 0; i < nrof_que_ids; i++)
        que_ss_map[i] = enr_ss_map[i];

    // ========== STEP 3: Blind ==========
    if (instr) inst.recordOfflineStepStart("step_blind");
    blindDatabaseOffline(enr_ss_map, gc_key, FIELD_MODULUS);
    if (instr) inst.recordOfflineStepEnd("step_blind");

    // ========== STEP 4: Share ==========
    // Sample per-row Shamir polynomials (token + label) for every enrolled item.
    // Sub-steps sample_coef_us / eval_shares_us are 0 — separating them would
    // require modifying core.cc::setShares (out of scope for this pass).
    if (instr) inst.recordOfflineStepStart("step_share");
    initializeSharePoly(FIELD_MODULUS);
    for (int pid = 0; pid < nrof_enr_ids; pid++)
        setShares(pid);
    if (instr) inst.recordOfflineStepEnd("step_share");

    // ========== STEP 5: Interp & Pack ==========
    if (instr) inst.recordOfflineStepStart("step_interp_pack");
    Timer t_interp;
    UVector4D partition_coeffs = computeCoefficients(db_partitions, enr_ss_map,
                                                     token_share_map, id_share_map);
    int64_t interp_us = t_interp.elapsed_us();
    if (instr) {
        inst.recordInterpPackSubStepTiming("interpolate", interp_us);
        inst.recordInterpPackRSSAfterInterp(getRSSMB());
    }

    Timer t_pack;
    UVector4D simd_partition_coeffs = simdPartitions(partition_coeffs);
    EncryptionParameters sc_params(scheme_type::bfv);
    SealCredentials sc_lpsi(sc_params, m, FIELD_MODULUS);
    PVector3D encoded_coeffs = encodeCoeffs(sc_lpsi, simd_partition_coeffs);
    int64_t pack_us = t_pack.elapsed_us();
    if (instr) {
        inst.recordInterpPackSubStepTiming("simd_pack", pack_us);
        inst.recordInterpPackRSSAfterPack(getRSSMB());
        inst.recordOfflineStepEnd("step_interp_pack");
    }

    // ========== STEP 6: 1-GC Query Blinding (session-scope; amortized per-query in step_gc below) ==========
    Timer t_gc;
    // Flatten all query subsamples in key order
    vector<uint64_t> flat_query;
    flat_query.reserve(static_cast<size_t>(nrof_que_ids) * inN);
    for (int i = 0; i < nrof_que_ids; i++)
        for (uint64_t v : que_ss_map[i])
            flat_query.push_back(v);

    // GC simulation (local, no network)
    vector<uint64_t> flat_blinded = gcSimulateLocal(gc_key, flat_query, FIELD_MODULUS);
    int64_t total_gc_us = t_gc.elapsed_us();

    // Scatter blinded values back into que_ss_map
    for (int i = 0; i < nrof_que_ids; i++)
        for (int j = 0; j < inN; j++)
            que_ss_map[i][j] = flat_blinded[static_cast<size_t>(i) * inN + j];

    // ========== ONLINE: Query Processing ==========
    vector<vector<int>> matching_ids(nrof_que_ids);
    if (instr) {
        // Sequential mode for instrumented runs: avoids per-thread timer
        // collisions on the shared Instrumentation object.  Inner OpenMP
        // parallelism (inside computeQueryPowers / homomorphicPolyEval /
        // etc.) is unaffected, so multi-thread benchmarks still scale.
        int64_t per_query_gc_us = total_gc_us / std::max(1, nrof_que_ids);
        for (int i = 0; i < nrof_que_ids; i++) {
            inst.startQuery(i, "TP");  // Synthetic bench uses true-positive queries

            // Step 6: amortized per-query GC time (1-GC was run once for all queries)
            inst.recordStepGC(per_query_gc_us, 0, 0, 0.0, getRSSMB());

            // Step 7: query powers (computeQueryPowers + simdQueryPowers + encryptQueryPowers)
            // Sub-split (r_encrypt vs r_serialize) deferred; report as r_encrypt_us.
            Timer t_qp;
            UVector2D query_powers = computeQueryPowers(que_ss_map[i]);
            UVector2D simd_query_powers = simdQueryPowers(query_powers);
            vector<Ciphertext> encrypted_query_powers = encryptQueryPowers(sc_lpsi, simd_query_powers);
            int64_t qp_us = t_qp.elapsed_us();
            inst.recordStepQueryPowers(qp_us, 0, 0, 0, 0, getRSSMB(), getRSSMB());

            // Step 8: per-round homomorphic evaluation + receiver decrypt
            // Per-round sub-split (coef_load / ctxt_inner_product / noise_flood / mod_switch / serialize)
            // deferred — would require touching sender.cc::homomorphicPolyEval which iterates over rounds.
            // Here we record one aggregate hom round.
            Timer t_hom;
            CVector2D enc_result = homomorphicPolyEval(sc_lpsi, encrypted_query_powers, encoded_coeffs);
            int64_t hom_us = t_hom.elapsed_us();
            Timer t_dec;
            UVector3D dec_result = decryptQueryResult(sc_lpsi, enc_result);
            int64_t dec_us = t_dec.elapsed_us();

            inst.startHomRound(0, "label");  // cli mode is K=1, T=1 → 1 round combining token+label
            inst.recordHomRoundSenderSubStep("ctxt_inner_product", hom_us);
            inst.recordHomRoundSenderSubStep("wall", hom_us);  // sub-step sum = wall (only ctxt_inner_product populated)
            inst.recordHomRoundReceiverSubStep(dec_us);
            inst.recordHomRoundBytes(0);  // No wire bytes in loopback mode
            inst.recordHomRoundRSSAfterRerand(getRSSMB());
            inst.endHomRound();

            // Step 9 + 10: token check + label recovery (combined in findMatches)
            Timer t_match;
            vector<int> matches = findMatches(dec_result);
            int64_t match_us = t_match.elapsed_us();
            inst.recordStepTokenCheck(match_us, 0, (int64_t)matches.size());
            inst.recordStepLabelRecovery(0, (int64_t)matches.size());

            matching_ids[i] = matches;

            bool matched = !matches.empty();
            inst.recordQueryResult(matched, matches);
            inst.endQuery();
        }
    } else {
        // Uninstrumented: original parallel path
        #pragma omp parallel for num_threads(nrof_online_threads)
        for (int i = 0; i < nrof_que_ids; i++) {
            matching_ids[i] = submitSingleQuery(sc_lpsi, que_ss_map[i], encoded_coeffs);
        }
    }

    auto t_total_end = high_resolution_clock::now();

    int FA = 0, FR = 0;
    for (int i = 0; i < nrof_que_ids; i++) {
        bool found = false;
        for (int mid : matching_ids[i]) {
            if (mid == i) found = true;
            else FA++;
        }
        if (!found) FR++;
    }

    double frr = 100.0 * FR / nrof_que_ids;
    double far = 100.0 * FA / ((double)nrof_que_ids * (nrof_enr_ids - 1));

    cout << "\n=== Synthetic Benchmark Results ===" << endl;
    cout << "Config:          " << config.getName() << endl;
    cout << "Enrolled IDs:    " << nrof_enr_ids << endl;
    cout << "Query IDs (TP):  " << nrof_que_ids << endl;
    cout << "N (subsamples):  " << inN << endl;
    cout << "K (threshold):   " << inK << endl;
    cout << endl;
    auto total_us = duration_cast<microseconds>(t_total_end - t_total_start).count();
    printf("Total wall time: %.2f ms\n", total_us / 1000.0);
    cout << endl;
    printf("FRR = %.4f%% (expected 0%%)\n", frr);
    printf("FAR = %.4f%%\n", far);

    if (FR > 0)
        cerr << "WARNING: FRR > 0 — " << FR << " true-positive queries missed!" << endl;
    else
        cout << "OK: FRR=0% confirmed for all " << nrof_que_ids << " true-positive queries." << endl;

    if (!opts.output_jsonl.empty()) {
        inst.finalize(opts.output_jsonl);
        cout << "\nInstrumentation output written to: " << opts.output_jsonl << endl;
    }

    cout << "\nSynthetic benchmark complete." << endl;
}

/**
 * Full PSI run reading integer-set data from CSV files.
 * CSV format: id,item0,item1,...,itemN-1
 */
void runCSV(const CLIOptions& opts) {
    cout << "\n=== CSTPSI CSV Run ===" << endl;

    ParamConfig config;
    config.loadFromJSON(opts.param_file);
    config.printSummary();

    if (!config.validate()) {
        cerr << "Parameter validation failed" << endl;
        exit(1);
    }

    config.applyToGlobals();
    config.initializeContext();

    string enr_path = opts.db_file.empty() ? config.getEnrollmentPath() : opts.db_file;
    string que_path = opts.query_file.empty() ? config.getQueryPath() : opts.query_file;

    if (!opts.db_file.empty()) validateFilePath(opts.db_file);
    if (!opts.query_file.empty()) validateFilePath(opts.query_file);
    if (!opts.output_file.empty()) validateFilePath(opts.output_file);

    I2SSM enr_ss_map;
    initializeSharePoly(FIELD_MODULUS);
    CSVDataLoader::loadEnrollmentCSV(enr_path, enr_ss_map, token_share_map, id_share_map);

    I32Vector3D db_partitions = parallelPartitionDB(enr_ss_map);

    I2SSM que_ss_map;
    CSVDataLoader::loadQueryCSV(que_path, que_ss_map);

    vector<int> query_keys;
    for (auto& kv : que_ss_map) query_keys.push_back(kv.first);
    std::sort(query_keys.begin(), query_keys.end());

    UVector4D partition_coeffs = computeCoefficients(db_partitions, enr_ss_map,
                                                     token_share_map, id_share_map);
    UVector4D simd_partition_coeffs = simdPartitions(partition_coeffs);

    EncryptionParameters sc_params(scheme_type::bfv);
    SealCredentials sc_lpsi(sc_params, m, FIELD_MODULUS);
    PVector3D encoded_coeffs = encodeCoeffs(sc_lpsi, simd_partition_coeffs);

    int actual_que = (int)que_ss_map.size();
    vector<vector<int>> matching_ids(actual_que);
    #pragma omp parallel for num_threads(nrof_online_threads)
    for (int i = 0; i < actual_que; i++)
        matching_ids[i] = submitSingleQuery(sc_lpsi, que_ss_map[query_keys[i]], encoded_coeffs);

    string out_path = opts.output_file.empty() ? config.getOutputPath() : opts.output_file;
    if (!out_path.empty()) {
        CSVDataLoader::writeResultsCSV(out_path, matching_ids);
    } else {
        cout << "Results (query_id: matched_ids):" << endl;
        for (int i = 0; i < actual_que; i++) {
            cout << i << ": ";
            for (int mid : matching_ids[i]) cout << mid << " ";
            cout << endl;
        }
    }

    cout << "\nCSV run complete." << endl;
}

int main(int argc, char* argv[]) {
    try {
        CLIOptions opts = parseArgs(argc, argv);

        if (opts.mode == "bench") {
            runStlpsiBench(opts);
        } else if (opts.mode == "csv_run") {
            runCSV(opts);
        } else {
            cerr << "Unknown mode: " << opts.mode << endl;
            printUsage();
            return 1;
        }

        return 0;

    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
}
