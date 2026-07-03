// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * CSTPSI Receiver Binary (Network Client)
 *
 * Copyright 2026 - CSTPSI Publication-Ready Infrastructure
 */

#include <iostream>
#include <string>
#include <chrono>
#include <memory>
#include <thread>

#include "receiver.h"
#include "network.h"
#include "serialization.h"
#include "param_loader.h"
#include "csv.h"
#include "aes_gc.h"

using namespace std;
using namespace std::chrono;
using namespace seal;
using namespace cstpsi;
using namespace cstpsi::gc;

// Global share maps defined in core.cc
extern I2SSM token_share_map;
extern I2SSM id_share_map;

struct ReceiverOptions {
    string query_file;
    string param_file;
    string sender_addr;
    string output_file;
    string output_dir;    // multi-round: write round_r.csv here instead of output_file
    int port = 1212;
    int timeout_ms = 30000;
    bool verbose = false;
    int threads_override = -1;
    int nrof_rounds = 0;  // 0 = single-round (uses output_file); >0 = multi-round (uses output_dir)
    int nrof_token_rounds = 1;  // NEW: default 1 (backward compat); T > 1 for multi-token FAR mitigation
    ProtocolMode mode = ProtocolMode::CSTPSI;  // Protocol mode: CSTPSI (optimized) or STLPSI (baseline)
};

void printUsage(const char* prog) {
    cout << "CSTPSI Receiver Binary (Network Client)" << endl;
    cout << "=======================================" << endl;
    cout << endl;
    cout << "Usage: " << prog << " [options]" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  --queryFile <path>   Query database file (required)" << endl;
    cout << "  --paramsFile <path>  Parameter configuration JSON (required)" << endl;
    cout << "  --senderAddr <host>  Sender hostname or IP address (required)" << endl;
    cout << "  --outputFile <path>  Output results file (single-round mode)" << endl;
    cout << "  --outputDir <dir>    Output directory for multi-round mode (writes round_N.csv)" << endl;
    cout << "  --nrofRounds N       Multi-round (1-GC) mode: N total rounds (token + label chunks)" << endl;
    cout << "  --nrofTokenRounds T  Multi-token FAR mitigation: T independent token rounds (default: 1)" << endl;
    cout << "  --mode <CSTPSI|STLPSI> Protocol mode (default: CSTPSI = 1GC + send-once; STLPSI = per-round GC + re-send)" << endl;
    cout << "  --port <port>        TCP port to connect to (default: 1212)" << endl;
    cout << "  --timeout <ms>       Operation timeout in ms (default: 30000)" << endl;
    cout << "  --nrof-online-threads <int>   Online-phase threads (default: cores-2)" << endl;
    cout << "  --verbose            Verbose output" << endl;
    cout << "  --help               Show this help message" << endl;
    cout << endl;
}

ReceiverOptions parseArgs(int argc, char* argv[]) {
    ReceiverOptions opts;

    if (argc < 2) {
        printUsage(argv[0]);
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--queryFile" && i + 1 < argc) {
            opts.query_file = argv[++i];
        } else if (arg == "--paramsFile" && i + 1 < argc) {
            opts.param_file = argv[++i];
        } else if (arg == "--senderAddr" && i + 1 < argc) {
            opts.sender_addr = argv[++i];
        } else if (arg == "--outputFile" && i + 1 < argc) {
            opts.output_file = argv[++i];
        } else if (arg == "--outputDir" && i + 1 < argc) {
            opts.output_dir = argv[++i];
        } else if (arg == "--nrofRounds" && i + 1 < argc) {
            opts.nrof_rounds = stoi(argv[++i]);
        } else if (arg == "--nrofTokenRounds" && i + 1 < argc) {
            opts.nrof_token_rounds = stoi(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            string mode_str = argv[++i];
            if (mode_str == "CSTPSI") {
                opts.mode = ProtocolMode::CSTPSI;
            } else if (mode_str == "STLPSI") {
                opts.mode = ProtocolMode::STLPSI;
            } else {
                cerr << "Error: --mode must be CSTPSI or STLPSI (got: " << mode_str << ")" << endl;
                printUsage(argv[0]);
                exit(1);
            }
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = stoi(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            opts.timeout_ms = stoi(argv[++i]);
        } else if (arg == "--nrof-online-threads" && i + 1 < argc) {
            opts.threads_override = stoi(argv[++i]);
        } else if (arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "--help") {
            printUsage(argv[0]);
            exit(0);
        } else {
            cerr << "Unknown option: " << arg << endl;
            printUsage(argv[0]);
            exit(1);
        }
    }

    if (opts.query_file.empty()) {
        cerr << "Error: --queryFile is required" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.param_file.empty()) {
        cerr << "Error: --paramsFile is required" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.sender_addr.empty()) {
        cerr << "Error: --senderAddr is required" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.nrof_rounds > 0) {
        if (opts.output_dir.empty()) {
            cerr << "Error: --outputDir is required when --nrofRounds > 0" << endl;
            printUsage(argv[0]);
            exit(1);
        }
    } else {
        if (opts.output_file.empty()) {
            cerr << "Error: --outputFile is required" << endl;
            printUsage(argv[0]);
            exit(1);
        }
    }

    if (opts.nrof_token_rounds > 1 && opts.nrof_rounds == 0) {
        cerr << "Error: --nrofTokenRounds > 1 requires --nrofRounds (multi-round mode)" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.nrof_rounds > 0 && opts.nrof_rounds < opts.nrof_token_rounds) {
        cerr << "Error: --nrofRounds must be >= --nrofTokenRounds" << endl;
        exit(1);
    }

    if (opts.mode == ProtocolMode::STLPSI && opts.nrof_rounds == 0) {
        cerr << "Error: --mode STLPSI requires --nrofRounds (multi-round mode)" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.mode == ProtocolMode::STLPSI && opts.nrof_token_rounds != 1) {
        cerr << "Error: --mode STLPSI requires --nrofTokenRounds 1 "
                "(multi-token-round T>1 is a CSTPSI optimization)" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    return opts;
}

/**
 * Helper function to send query with retry logic
 */
bool sendQueryWithRetry(
    NetworkClient& client,
    const vector<Ciphertext>& query,
    CVector2D& response,
    shared_ptr<SEALContext> context,
    int max_retries = 3,
    bool verbose = false
) {
    int base_delay_ms = 1000;

    // ZMQ REQ socket requires strict send->recv alternation, so every round
    // sends then receives. Under send-once the caller passes a FULL query on the
    // first round and an EMPTY query (tiny trigger) on later rounds; the sender
    // caches the full query and reuses it, ignoring the trigger payload.
    for (int attempt = 0; attempt < max_retries; attempt++) {
        try {
            client.sendQuery(query);
            response = client.receiveResponse(context);
            return true;

        } catch (const NetworkTimeoutException& e) {
            int delay_ms = base_delay_ms * (1 << attempt);
            cerr << "Query timeout (attempt " << (attempt + 1)
                 << "/" << max_retries << "), retrying in "
                 << delay_ms << "ms..." << endl;

            if (attempt < max_retries - 1) {
                this_thread::sleep_for(chrono::milliseconds(delay_ms));
                try {
                    client.reconnect();
                } catch (const exception& e) {
                    cerr << "Reconnection failed: " << e.what() << endl;
                }
            }

        } catch (const NetworkException& e) {
            cerr << "Network error: " << e.what() << endl;
            return false;
        }
    }

    cerr << "Query failed after " << max_retries << " attempts" << endl;
    return false;
}

int main(int argc, char* argv[]) {
    try {
        ReceiverOptions opts = parseArgs(argc, argv);

        cout << "=== CSTPSI Receiver (Network Client) ===" << endl;
        cout << endl;

        cout << "Loading parameters from: " << opts.param_file << endl;
        ParamConfig config;
        config.loadFromJSON(opts.param_file);
        config.printSummary();

        if (!config.validate()) {
            cerr << "Parameter validation failed" << endl;
            return 1;
        }

        config.applyToGlobals();
        if (opts.threads_override > 0) nrof_online_threads = opts.threads_override;
        config.initializeContext();

        cout << "\n=== CSTPSI Parameters ===" << endl;
        cout << "N (database partitions): " << inN << endl;
        cout << "m (SIMD): " << m << endl;
        cout << "poly_modulus_degree: " << config.getPolyModulusDegree() << endl;
        cout << "plain_modulus: " << FIELD_MODULUS << endl;
        cout << "partition_size: " << partition_size << endl;
        cout << "=========================" << endl << endl;

        cout << "Loading query database from: " << opts.query_file << endl;
        I2SSM que_ss_map;
        CSVDataLoader::loadQueryCSV(opts.query_file, que_ss_map);
        cout << "  Loaded " << que_ss_map.size() << " query records" << endl;

        vector<int> query_keys;
        for (auto& kv : que_ss_map) query_keys.push_back(kv.first);
        std::sort(query_keys.begin(), query_keys.end());
        int actual_nrof_que_ids = static_cast<int>(query_keys.size());

        cout << "\n[Local] Setting up SEAL context..." << endl;
        auto t1 = high_resolution_clock::now();
        EncryptionParameters sc_params(scheme_type::bfv);
        SealCredentials sc_lpsi(sc_params, m, FIELD_MODULUS);
        auto t2 = high_resolution_clock::now();
        long seal_ms = duration_cast<milliseconds>(t2 - t1).count();
        cout << "  Time: " << seal_ms << " ms" << endl;

        cout << "\nConnecting to sender at " << opts.sender_addr
             << ":" << opts.port << endl;
        unique_ptr<NetworkClient> client;
        try {
            client = make_unique<NetworkClient>(opts.sender_addr, opts.port, opts.timeout_ms);
        } catch (const NetworkException& e) {
            cerr << "Failed to connect to sender: " << e.what() << endl;
            return 1;
        }

        if (!client->isConnected()) {
            cerr << "Failed to connect to sender" << endl;
            return 1;
        }

        cout << "Connected to sender. Sending session setup..." << endl;

        // Send total subsample item count: number of query IDs × inN subsamples per ID
        int total_query_items = actual_nrof_que_ids * inN;
        client->sendSessionSetup(total_query_items);
        // nrof_real_partitions: used to drop phantom SIMD-padding rows from the
        // decrypted result before matching (see decryptQueryResult). -1 if a
        // legacy sender didn't provide it (then no slicing).
        int nrof_real_partitions = client->receiveSessionAck();

        // Pre-allocate for encrypted queries (to be computed per-round or once upfront)
        vector<vector<Ciphertext>> all_encrypted_queries;

        // If mode is CSTPSI: single GC upfront, then pre-encrypt queries for all rounds
        if (opts.mode == ProtocolMode::CSTPSI) {
            cout << "Session setup complete. Running GC blinding..." << endl;
            auto t_gc_start = high_resolution_clock::now();

            // Flatten all query subsamples in key order
            vector<uint64_t> flat_query;
            flat_query.reserve(static_cast<size_t>(actual_nrof_que_ids) * inN);
            for (int i = 0; i < actual_nrof_que_ids; i++)
                for (uint64_t v : que_ss_map[query_keys[i]])
                    flat_query.push_back(v);

            // GC blinding via EMP semi-honest 2PC (runs ONCE for all rounds)
            vector<uint64_t> flat_blinded = gcReceiverRole(
                flat_query, opts.sender_addr, opts.port + 1, FIELD_MODULUS
            );

            // Scatter blinded values back into que_ss_map
            for (int i = 0; i < actual_nrof_que_ids; i++)
                for (int j = 0; j < inN; j++)
                    que_ss_map[query_keys[i]][j] = flat_blinded[static_cast<size_t>(i) * inN + j];

            auto t_gc_end = high_resolution_clock::now();
            long gc_ms = duration_cast<milliseconds>(t_gc_end - t_gc_start).count();
            cout << "GC blinding complete (" << gc_ms << " ms). Processing FHE queries..." << endl << endl;

            // Pre-encrypt query powers once (same blinded values reused across all rounds)
            all_encrypted_queries.resize(actual_nrof_que_ids);
            {
                auto t_enc = high_resolution_clock::now();
                for (int i = 0; i < actual_nrof_que_ids; i++) {
                    UVector2D qp = computeQueryPowers(que_ss_map[query_keys[i]]);
                    UVector2D sp = simdQueryPowers(qp);
                    all_encrypted_queries[i] = encryptQueryPowers(sc_lpsi, sp);
                }
                auto t_enc_end = high_resolution_clock::now();
                if (opts.verbose)
                    cout << "Query encryption: "
                         << duration_cast<milliseconds>(t_enc_end - t_enc).count() << " ms" << endl;
            }
        } else {
            // If mode is STLPSI: defer GC to per-round, keep original query for each round
            cout << "Session setup complete. Deferring GC to per-round (STLPSI baseline)..." << endl << endl;
        }

        int T = opts.nrof_token_rounds;
        int total_rounds = (opts.nrof_rounds > 0) ? opts.nrof_rounds : 1;
        int K = (opts.nrof_rounds > 0) ? (total_rounds - T) : 0;  // label chunk rounds (only in multi-round mode)

        // send-once is the CSTPSI optimization; off in STLPSI mode
        bool send_once = (opts.mode == ProtocolMode::CSTPSI);
        // Tiny trigger sent on rounds >=1 under send-once (serializes as count=0);
        // the sender reuses its cached query and ignores this payload.
        const vector<Ciphertext> EMPTY_QUERY;

        auto t_start = high_resolution_clock::now();

        // Receiver-side plaintext reconstruction cost (ablation: "reconstruction
        // cache" step). recon_fvp_us = wall time in findVerifiedPairs, the per-round
        // pair verification (binom(inN,2) repK2/inverse per partition) that the
        // unoptimized base would recompute every round but the cache derives once.
        // recon_other_us = token-round findMatches + label-round reconstructLabels.
        // These bracket the call sites (the OpenMP parallelism is inside the
        // functions, so wall-timing the call is single-threaded and safe).
        long long recon_fvp_us = 0, recon_other_us = 0;

        if (T > 1 && opts.nrof_rounds > 0) {
            // Multi-token-round mode (with multi-round/1-GC)
            if (opts.verbose)
                cout << "Multi-token FAR mitigation: T=" << T << " token rounds + "
                     << K << " label rounds" << endl;

            // Storage for per-query verified pairs (computed after all token rounds)
            vector<vector<PairSet>> all_verified_pairs(actual_nrof_que_ids);

            // Storage for token round results per query
            // token_round_results[qi][t] = UVector3D for query qi, token round t
            vector<vector<UVector3D>> token_round_results(actual_nrof_que_ids, vector<UVector3D>(T));

            // Phase 1: Process T token rounds
            for (int t = 0; t < T; t++) {
                if (opts.verbose)
                    cout << "Token round " << t << "/" << T << "..." << endl;

                vector<vector<int>> round_matching_ids(actual_nrof_que_ids);

                for (int qi = 0; qi < actual_nrof_que_ids; qi++) {
                    if (opts.verbose) {
                        cout << "  Query " << qi << "/" << actual_nrof_que_ids << endl;
                    }

                    try {
                        CVector2D encrypted_result;

                        // Send full query only on the first overall round (token t==0);
                        // later rounds send the empty trigger under send-once.
                        bool send_full = !send_once || (t == 0);
                        const vector<Ciphertext>& to_send = send_full ? all_encrypted_queries[qi] : EMPTY_QUERY;

                        if (!sendQueryWithRetry(*client, to_send,
                                               encrypted_result, sc_lpsi.context, 3, opts.verbose)) {
                            cerr << "Failed to process token round " << t << " query " << qi << ", aborting." << endl;
                            return 1;
                        }

                        token_round_results[qi][t] = decryptQueryResult(sc_lpsi, encrypted_result, nrof_real_partitions);
                        auto _rec0 = high_resolution_clock::now();
                        round_matching_ids[qi] = findMatches(token_round_results[qi][t]);
                        recon_other_us += duration_cast<microseconds>(high_resolution_clock::now() - _rec0).count();

                    } catch (const exception& e) {
                        cerr << "Error on token round " << t << " query " << qi
                             << ": " << e.what() << endl;
                        return 1;
                    }
                }

                // Write token round results for verification/FAR measurement
                string round_csv = opts.output_dir + "/round_" + to_string(t) + ".csv";
                CSVDataLoader::writeResultsCSV(round_csv, round_matching_ids);
                if (opts.verbose)
                    cout << "  Token round " << t << " written to " << round_csv << endl;
            }

            // Compute verified pairs (intersection across T token rounds)
            if (opts.verbose)
                cout << "Computing verified pairs via intersection across " << T << " token rounds..." << endl;

            auto _fvp0 = high_resolution_clock::now();
            #pragma omp parallel for num_threads(nrof_online_threads)
            for (int qi = 0; qi < actual_nrof_que_ids; qi++) {
                all_verified_pairs[qi] = findVerifiedPairs(token_round_results[qi]);
            }
            recon_fvp_us += duration_cast<microseconds>(high_resolution_clock::now() - _fvp0).count();

            // Phase 2: Process K label rounds
            for (int r = 0; r < K; r++) {
                if (opts.verbose)
                    cout << "Label round " << r << "/" << K << "..." << endl;

                vector<vector<int>> round_matching_ids(actual_nrof_que_ids);

                for (int qi = 0; qi < actual_nrof_que_ids; qi++) {
                    if (opts.verbose) {
                        cout << "  Query " << qi << "/" << actual_nrof_que_ids << endl;
                    }

                    try {
                        CVector2D encrypted_result;

                        // Label rounds are overall round T+r >= 1, so under send-once
                        // the full query was already sent at token round 0: send the trigger.
                        const vector<Ciphertext>& to_send = send_once ? EMPTY_QUERY : all_encrypted_queries[qi];

                        if (!sendQueryWithRetry(*client, to_send,
                                               encrypted_result, sc_lpsi.context, 3, opts.verbose)) {
                            cerr << "Failed to process label round " << r << " query " << qi << ", aborting." << endl;
                            return 1;
                        }

                        UVector3D label_result = decryptQueryResult(sc_lpsi, encrypted_result, nrof_real_partitions);
                        auto _rec0 = high_resolution_clock::now();
                        round_matching_ids[qi] = reconstructLabels(all_verified_pairs[qi], label_result);
                        recon_other_us += duration_cast<microseconds>(high_resolution_clock::now() - _rec0).count();

                    } catch (const exception& e) {
                        cerr << "Error on label round " << r << " query " << qi
                             << ": " << e.what() << endl;
                        return 1;
                    }
                }

                string round_csv = opts.output_dir + "/round_" + to_string(T + r) + ".csv";
                CSVDataLoader::writeResultsCSV(round_csv, round_matching_ids);
                if (opts.verbose)
                    cout << "  Label round " << r << " written to " << round_csv << endl;
            }

        } else {
            // Original single-token path (T=1)
            int nrof_rounds = total_rounds;

            for (int r = 0; r < nrof_rounds; r++) {
                if (opts.verbose)
                    cout << "Round " << r << "/" << nrof_rounds << "..." << endl;

                // If mode is STLPSI: run per-round GC and encrypt
                if (opts.mode == ProtocolMode::STLPSI) {
                    auto t_gc_start = high_resolution_clock::now();

                    // Flatten all query subsamples in key order
                    vector<uint64_t> flat_query;
                    flat_query.reserve(static_cast<size_t>(actual_nrof_que_ids) * inN);
                    for (int i = 0; i < actual_nrof_que_ids; i++)
                        for (uint64_t v : que_ss_map[query_keys[i]])
                            flat_query.push_back(v);

                    // GC blinding via EMP semi-honest 2PC (runs ONCE per round in FLPSI'21 mode)
                    vector<uint64_t> flat_blinded = gcReceiverRole(
                        flat_query, opts.sender_addr, opts.port + 1, FIELD_MODULUS
                    );

                    // Scatter blinded values back into a per-round query map
                    I2SSM round_que_ss_map;
                    for (int i = 0; i < actual_nrof_que_ids; i++) {
                        round_que_ss_map[query_keys[i]].resize(inN);
                        for (int j = 0; j < inN; j++)
                            round_que_ss_map[query_keys[i]][j] = flat_blinded[static_cast<size_t>(i) * inN + j];
                    }

                    auto t_gc_end = high_resolution_clock::now();
                    long gc_ms = duration_cast<milliseconds>(t_gc_end - t_gc_start).count();
                    if (opts.verbose)
                        cout << "  GC blinding complete (" << gc_ms << " ms)" << endl;

                    // Encrypt queries for this round
                    all_encrypted_queries.clear();
                    all_encrypted_queries.resize(actual_nrof_que_ids);
                    for (int i = 0; i < actual_nrof_que_ids; i++) {
                        UVector2D qp = computeQueryPowers(round_que_ss_map[query_keys[i]]);
                        UVector2D sp = simdQueryPowers(qp);
                        all_encrypted_queries[i] = encryptQueryPowers(sc_lpsi, sp);
                    }
                }

                vector<vector<int>> matching_ids(actual_nrof_que_ids);
                int query_count = 0;

                for (int i = 0; i < actual_nrof_que_ids; i++) {
                    if (opts.verbose) {
                        cout << "  Query " << i << "/" << actual_nrof_que_ids << endl;
                    }

                    try {
                        CVector2D encrypted_result;

                        // Send full query only on the first round; trigger thereafter under send-once.
                        bool send_full = !send_once || (r == 0);
                        const vector<Ciphertext>& to_send = send_full ? all_encrypted_queries[i] : EMPTY_QUERY;

                        if (!sendQueryWithRetry(*client, to_send,
                                               encrypted_result, sc_lpsi.context, 3, opts.verbose)) {
                            cerr << "Failed to process round " << r << " query " << i << ", aborting." << endl;
                            return 1;
                        }

                        UVector3D result = decryptQueryResult(sc_lpsi, encrypted_result, nrof_real_partitions);
                        auto _rec0 = high_resolution_clock::now();
                        matching_ids[i] = findMatches(result);
                        recon_other_us += duration_cast<microseconds>(high_resolution_clock::now() - _rec0).count();
                        query_count++;

                    } catch (const exception& e) {
                        cerr << "Error on round " << r << " query " << i
                             << ": " << e.what() << endl;
                        return 1;
                    }
                }

                // Write results for this round
                if (opts.nrof_rounds > 0) {
                    string round_csv = opts.output_dir + "/round_" + to_string(r) + ".csv";
                    CSVDataLoader::writeResultsCSV(round_csv, matching_ids);
                    if (opts.verbose)
                        cout << "  Round " << r << " written to " << round_csv << endl;
                } else {
                    // Single-round: write to output_file (only r=0 executed)
                    cout << "\nWriting results to " << opts.output_file << endl;
                    CSVDataLoader::writeResultsCSV(opts.output_file, matching_ids);
                }
            }
        }

        auto t_end = high_resolution_clock::now();
        long total_ms = duration_cast<milliseconds>(t_end - t_start).count();

        cout << "\nQuery processing complete." << endl;
        cout << "Total time: " << total_ms << " ms" << endl;
        // Reconstruction-cache instrumentation (us, summed over all queries/rounds):
        // fvp = per-round pair verification cached by the recon step; other = token
        // findMatches + label reconstructLabels. Parsed by experiments/lib/run_cell.sh.
        cout << "Recon us: fvp=" << recon_fvp_us << " other=" << recon_other_us
             << " total=" << (recon_fvp_us + recon_other_us) << endl;
        cout << "NET bytes_sent: " << client->getSentBytes() << endl;
        cout << "NET bytes_received: " << client->getReceivedBytes() << endl;

        cout << "Done." << endl;
        return 0;

    } catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
}
