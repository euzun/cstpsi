// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * CSTPSI Sender Binary (Network Server)
 *
 * Copyright 2026 - CSTPSI Publication-Ready Infrastructure
 */

#include <iostream>
#include <string>
#include <chrono>
#include <csignal>
#include <memory>
#include <thread>

#include "sender.h"
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

// Signal handler flag
volatile sig_atomic_t stop_flag = 0;

void signal_handler(int signal) {
    cout << "\nReceived signal " << signal << ", shutting down..." << endl;
    stop_flag = 1;
}

struct SenderOptions {
    string db_file;
    string param_file;
    int port = 1212;
    string bind_addr = "0.0.0.0";
    bool verbose = false;
    int label_col_index = -1;
    int threads_override = -1;
    int prep_threads_override = -1;
    int nrof_label_chunks = 0;  // 0 = single-round mode; >0 = multi-round (1-GC)
    int nrof_token_rounds = 1;  // NEW: default 1 (backward compat); T > 1 for multi-token FAR mitigation
    ProtocolMode mode = ProtocolMode::CSTPSI;  // Protocol mode: CSTPSI (optimized) or STLPSI (baseline)
};

void printUsage(const char* prog) {
    cout << "CSTPSI Sender Binary (Network Server)" << endl;
    cout << "=====================================" << endl;
    cout << endl;
    cout << "Usage: " << prog << " [options]" << endl;
    cout << endl;
    cout << "Options:" << endl;
    cout << "  --dbFile <path>      Enrollment database file (required)" << endl;
    cout << "  --paramsFile <path>  Parameter configuration JSON (required)" << endl;
    cout << "  --port <port>        TCP port to listen on (default: 1212)" << endl;
    cout << "  --bind <addr>        Network interface to bind to (default: 0.0.0.0)" << endl;
    cout << "  --labelCol <int>     Label column index in CSV (-1 = use id, default: -1)" << endl;
    cout << "  --nrofLabelChunks K  Multi-round (1-GC) mode: K label-chunk rounds + T token rounds" << endl;
    cout << "                       Enrollment CSV must have label cols at columns 1+inN..1+inN+K-1" << endl;
    cout << "  --nrofTokenRounds T  Multi-token FAR mitigation: T independent token rounds (default: 1)" << endl;
    cout << "  --mode <CSTPSI|STLPSI> Protocol mode (default: CSTPSI = 1GC + send-once; STLPSI = per-round GC + re-send)" << endl;
    cout << "  --nrof-online-threads <int>   Online-phase threads (default: cores-2)" << endl;
    cout << "  --nrof-offline-threads <int>  Offline-preprocessing threads (default: all cores)" << endl;
    cout << "  --verbose            Verbose output" << endl;
    cout << "  --help               Show this help message" << endl;
    cout << endl;
}

SenderOptions parseArgs(int argc, char* argv[]) {
    SenderOptions opts;

    if (argc < 2) {
        printUsage(argv[0]);
        exit(1);
    }

    for (int i = 1; i < argc; i++) {
        string arg = argv[i];

        if (arg == "--dbFile" && i + 1 < argc) {
            opts.db_file = argv[++i];
        } else if (arg == "--paramsFile" && i + 1 < argc) {
            opts.param_file = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            opts.port = stoi(argv[++i]);
        } else if (arg == "--bind" && i + 1 < argc) {
            opts.bind_addr = argv[++i];
        } else if (arg == "--labelCol" && i + 1 < argc) {
            opts.label_col_index = stoi(argv[++i]);
        } else if (arg == "--nrofLabelChunks" && i + 1 < argc) {
            opts.nrof_label_chunks = stoi(argv[++i]);
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
        } else if (arg == "--nrof-online-threads" && i + 1 < argc) {
            opts.threads_override = stoi(argv[++i]);
        } else if (arg == "--nrof-offline-threads" && i + 1 < argc) {
            opts.prep_threads_override = stoi(argv[++i]);
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

    if (opts.db_file.empty()) {
        cerr << "Error: --dbFile is required" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.param_file.empty()) {
        cerr << "Error: --paramsFile is required" << endl;
        printUsage(argv[0]);
        exit(1);
    }

    if (opts.mode == ProtocolMode::STLPSI && opts.nrof_label_chunks == 0) {
        cerr << "Error: --mode STLPSI requires --nrofLabelChunks (multi-round mode)" << endl;
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

int main(int argc, char* argv[]) {
    try {
        SenderOptions opts = parseArgs(argc, argv);

        cout << "=== CSTPSI Sender (Network Server) ===" << endl;
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
        if (opts.prep_threads_override > 0) nrof_offline_threads = opts.prep_threads_override;
        config.initializeContext();

        cout << "\n=== CSTPSI Parameters ===" << endl;
        cout << "N (database partitions): " << inN << endl;
        cout << "m (SIMD): " << m << endl;
        cout << "poly_modulus_degree: " << config.getPolyModulusDegree() << endl;
        cout << "plain_modulus: " << FIELD_MODULUS << endl;
        cout << "partition_size: " << partition_size << endl;
        cout << "=========================" << endl << endl;

        cout << "Loading enrollment database from: " << opts.db_file << endl;
        I2SSM enr_ss_map;
        initializeSharePoly(FIELD_MODULUS);
        CSVDataLoader::loadEnrollmentCSV(opts.db_file, enr_ss_map, token_share_map, id_share_map, opts.label_col_index);
        cout << "  Loaded " << enr_ss_map.size() << " enrollment records" << endl;

        // Generate additional token share maps for multi-token FAR mitigation
        vector<I2SSM> token_share_maps(opts.nrof_token_rounds);
        token_share_maps[0] = token_share_map;  // First token sharing from loadEnrollmentCSV

        if (opts.nrof_token_rounds > 1) {
            cout << "Generating " << (opts.nrof_token_rounds - 1)
                 << " additional independent token share maps..." << endl;
            for (int t = 1; t < opts.nrof_token_rounds; t++) {
                for (auto& [id, items] : enr_ss_map) {
                    unsigned long *shares = createSecretShares(token, inN, inK, 0);
                    for (int i = 0; i < inN; i++)
                        token_share_maps[t][id].push_back(shares[i]);
                    free(shares);
                }
            }
        }

        // GC blinding runs FIRST, so partitioning operates on the BLINDED values.
        // blindItemOffline is AES-128 then reduce mod FIELD_MODULUS (aes_key.h),
        // which is many-to-one (2^128 -> ~2^23): two DISTINCT raw subsamples can
        // collide *after* blinding. Partitioning on the blinded values lets
        // isDistinct separate those collisions too, guaranteeing distinct
        // interpolation x-coordinates within every partition -- so
        // getSplitCoefficients never has to overwrite a real subject's x. (Still
        // before computeCoefficients, so the FHE polynomial encodes blinded values
        // that the receiver's GC-blinded query items are roots of.)
        cout << "\n[1/3] GC database blinding..." << endl;
        auto t1 = high_resolution_clock::now();

        AesKey gc_key = generateAesKey();
        blindDatabaseOffline(enr_ss_map, gc_key, FIELD_MODULUS);

        auto t2 = high_resolution_clock::now();
        long blind_ms = duration_cast<milliseconds>(t2 - t1).count();
        cout << "  Time: " << blind_ms << " ms" << endl;

        cout << "[2/3] Partitioning database (on blinded values)..." << endl;
        t1 = high_resolution_clock::now();

        I32Vector3D db_partitions = parallelPartitionDB(enr_ss_map);

        t2 = high_resolution_clock::now();
        long partition_ms = duration_cast<milliseconds>(t2 - t1).count();
        cout << "  Time: " << partition_ms << " ms" << endl;

        // Count REAL partitions (flattened across splits). The SIMD result the
        // receiver decrypts has m/inN rows per ciphertext-part, but only these
        // hold real subjects; the rest are dummy padding. Sent in the session ACK
        // so the receiver can drop the phantom rows before matching (else they
        // reconstruct token=0 round-invariantly and survive T-round amplification).
        int nrof_real_partitions = 0;
        for (auto& split : db_partitions) nrof_real_partitions += (int)split.size();

        // ── Multi-round (1-GC) offline: build T+K coefficient sets ──
        // Rounds 0..T-1 = independent token rounds; rounds T..T+K-1 = label chunks (cols 1+inN..1+inN+K-1 in enr CSV).
        // ── Single-round offline: build 1 coefficient set ──
        EncryptionParameters sc_params(scheme_type::bfv);
        SealCredentials sc_lpsi(sc_params, m, FIELD_MODULUS);

        vector<PVector3D> encoded_coeffs_all;  // used in multi-round mode
        PVector3D encoded_coeffs_single;        // used in single-round mode

        if (opts.nrof_label_chunks > 0) {
            int K = opts.nrof_label_chunks;
            int T = opts.nrof_token_rounds;
            cout << "[3/3] Computing " << (T + K) << " coefficient sets (multi-round: "
                 << T << " token rounds + " << K << " label chunks)..." << endl;

            // Rounds 0..T-1: token rounds (T independent sharings of secret=0)
            for (int t = 0; t < T; t++) {
                t1 = high_resolution_clock::now();
                UVector4D coeffs = computeCoefficients(db_partitions, enr_ss_map,
                                                       token_share_maps[t], token_share_maps[t]);
                UVector4D simd_coeffs = simdPartitions(coeffs);
                encoded_coeffs_all.push_back(encodeCoeffs(sc_lpsi, simd_coeffs));
                t2 = high_resolution_clock::now();
                cout << "  Time: " << duration_cast<milliseconds>(t2 - t1).count()
                     << " ms (token round " << t << "/" << T << ")" << endl;
            }

            // Rounds T..T+K-1: label chunks (label column 1+inN + r)
            for (int r = 0; r < K; r++) {
                t1 = high_resolution_clock::now();
                I2SSM round_id_sm;
                CSVDataLoader::loadIdSharesOnly(opts.db_file, round_id_sm, 1 + inN + r);
                // Use first token sharing as gate for all label rounds
                UVector4D coeffs = computeCoefficients(db_partitions, enr_ss_map,
                                                       token_share_maps[0], round_id_sm);
                UVector4D simd_coeffs = simdPartitions(coeffs);
                encoded_coeffs_all.push_back(encodeCoeffs(sc_lpsi, simd_coeffs));
                t2 = high_resolution_clock::now();
                cout << "  Time: " << duration_cast<milliseconds>(t2 - t1).count()
                     << " ms (label chunk " << (r + 1) << "/" << K << ")" << endl;
            }
        } else {
            cout << "[3/3] SEAL context and coefficient encoding..." << endl;
            t1 = high_resolution_clock::now();

            UVector4D partition_coeffs = computeCoefficients(db_partitions, enr_ss_map,
                                                              token_share_map, id_share_map);
            UVector4D simd_partition_coeffs = simdPartitions(partition_coeffs);
            encoded_coeffs_single = encodeCoeffs(sc_lpsi, simd_partition_coeffs);

            t2 = high_resolution_clock::now();
            long seal_ms = duration_cast<milliseconds>(t2 - t1).count();
            cout << "  Time: " << seal_ms << " ms" << endl;
        }

        // Offline preprocessing complete: free the enrollment + share maps and the
        // raw partitions. They are read only during offline coefficient computation
        // (never during online query serving, which uses encoded_coeffs_* only), so
        // releasing them here shrinks the sender's resident set substantially at
        // large D (~2 GB at D=1M) and avoids macOS memory-compression thrash that
        // otherwise stalls the per-query evaluation.
        enr_ss_map = I2SSM();
        token_share_map = I2SSM();
        id_share_map = I2SSM();
        token_share_maps = vector<I2SSM>();
        db_partitions = I32Vector3D();

        cout << "\nPreprocessing complete." << endl;

        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);

        cout << "\nStarting network server..." << endl;
        unique_ptr<NetworkServer> server;
        try {
            server = make_unique<NetworkServer>(opts.port, opts.bind_addr);
        } catch (const NetworkException& e) {
            cerr << "Failed to start server: " << e.what() << endl;
            return 1;
        }

        cout << "Sender listening on " << opts.bind_addr << ":" << opts.port << endl;
        cout << "Ready to accept queries. Press Ctrl+C to exit." << endl;

        int query_count = 0;
        int nrof_rounds = (opts.nrof_label_chunks > 0)
            ? (opts.nrof_token_rounds + opts.nrof_label_chunks)
            : 1;

        while (!stop_flag && server->isRunning()) {
            try {
                // ── Session setup ──
                int receiver_item_count;
                try {
                    receiver_item_count = server->receiveSessionSetup();
                } catch (const NetworkTimeoutException&) {
                    if (stop_flag) break;
                    continue;  // idle: no client yet, just re-poll stop_flag
                } catch (const NetworkException& e) {
                    if (stop_flag) break;
                    cerr << "SESSION_SETUP error: " << e.what() << endl;
                    continue;
                }

                if (opts.verbose)
                    cout << "Receiver has " << receiver_item_count << " query items." << endl;

                // ── GC phase ──
                // If mode is CSTPSI: single GC upfront, then K+1 FHE rounds
                // If mode is STLPSI: K+1 GCs interleaved with FHE rounds
                if (opts.mode == ProtocolMode::CSTPSI) {
                    // Single bundled GC (current 1-GC optimization)
                    int emp_port = opts.port + 1;
                    std::exception_ptr gc_ex;
                    std::thread gc_thread([&gc_key, emp_port, receiver_item_count, &gc_ex]() {
                        try {
                            gcSenderRole(gc_key, emp_port, receiver_item_count, FIELD_MODULUS);
                        } catch (...) {
                            gc_ex = std::current_exception();
                        }
                    });

                    // ACK unblocks receiver to connect to EMP port
                    server->sendSessionAck(nrof_real_partitions);

                    // Wait for GC to finish
                    gc_thread.join();
                    if (gc_ex) {
                        try { std::rethrow_exception(gc_ex); }
                        catch (const std::exception& e) {
                            cerr << "GC sender error: " << e.what() << endl;
                        }
                        continue;
                    }

                    if (opts.verbose)
                        cout << "GC phase complete. Processing FHE queries..." << endl;
                } else {
                    // STLPSI baseline: defer GC to per-round
                    server->sendSessionAck(nrof_real_partitions);
                    if (opts.verbose)
                        cout << "Session ACK sent. Will run GC per-round (STLPSI baseline)..." << endl;
                }

                // ── FHE query loop ──
                // receiver_item_count = nrof_queries * inN.
                int nrof_fhe_queries = receiver_item_count / inN;
                bool session_ok = true;

                // send-once is the CSTPSI optimization; off in STLPSI mode
                bool send_once = (opts.mode == ProtocolMode::CSTPSI);

                // Query cache for send_once optimization
                std::vector<std::vector<seal::Ciphertext>> query_cache;
                if (send_once) {
                    query_cache.resize(nrof_fhe_queries);
                }

                for (int r = 0; r < nrof_rounds && !stop_flag && session_ok; r++) {
                    // If mode is STLPSI: run per-round GC before FHE queries for this round
                    if (opts.mode == ProtocolMode::STLPSI) {
                        int emp_port = opts.port + 1;
                        std::exception_ptr gc_ex;
                        std::thread gc_thread([&gc_key, emp_port, receiver_item_count, &gc_ex]() {
                            try {
                                gcSenderRole(gc_key, emp_port, receiver_item_count, FIELD_MODULUS);
                            } catch (...) {
                                gc_ex = std::current_exception();
                            }
                        });

                        // Wait for GC to finish before processing queries
                        gc_thread.join();
                        if (gc_ex) {
                            try { std::rethrow_exception(gc_ex); }
                            catch (const std::exception& e) {
                                cerr << "GC sender error on round " << r << ": " << e.what() << endl;
                            }
                            session_ok = false;
                            break;
                        }

                        if (opts.verbose)
                            cout << "Round " << r << " GC complete. Processing FHE queries..." << endl;
                    }

                    PVector3D& coeffs = (opts.nrof_label_chunks > 0)
                        ? encoded_coeffs_all[r]
                        : encoded_coeffs_single;

                    for (int qi = 0; qi < nrof_fhe_queries && !stop_flag; qi++) {
                        try {
                            // ZMQ REP requires recv before send, so ALWAYS receive each round.
                            // Under send-once: round 0 carries the full query (cache it); later
                            // rounds carry a tiny empty trigger (ignored) and we evaluate the cache.
                            //
                            // The server socket has a short rcvtimeo so the idle accept loop can
                            // poll stop_flag; that same timeout fires here whenever the receiver
                            // is quiet mid-session (e.g. computing verified pairs at the
                            // token->label boundary). A timeout is NOT an error in-session: keep
                            // re-polling until a query arrives or we're asked to stop, so an
                            // inter-phase gap of any length can't desync the REQ/REP exchange.
                            std::vector<seal::Ciphertext> received;
                            while (true) {
                                try {
                                    received = server->receiveQuery(sc_lpsi.context);
                                    break;
                                } catch (const NetworkTimeoutException&) {
                                    if (stop_flag) break;
                                    // receiver still working; wait for the next query
                                }
                            }
                            if (stop_flag) break;
                            if (send_once && r == 0) {
                                query_cache[qi] = received;
                            }
                            std::vector<seal::Ciphertext>& encrypted_query =
                                send_once ? query_cache[qi] : received;

                            auto eval_start = high_resolution_clock::now();
                            CVector2D encrypted_result = homomorphicPolyEval(
                                sc_lpsi, encrypted_query, coeffs
                            );
                            auto eval_end = high_resolution_clock::now();
                            long eval_ms = duration_cast<milliseconds>(eval_end - eval_start).count();

                            server->sendResponse(encrypted_result);

                            if (opts.verbose)
                                cout << "[Round " << r << " Query " << query_count << "] Done in "
                                     << eval_ms << " ms" << endl;

                            query_count++;

                        } catch (const NetworkException& e) {
                            cerr << "Network error on round " << r << " query " << qi
                                 << ": " << e.what() << endl;
                            try { server->sendError(ErrorCode::EVALUATION_ERROR, e.what()); } catch (...) {}
                            session_ok = false;
                            break;
                        } catch (const exception& e) {
                            cerr << "Error on round " << r << " query " << qi
                                 << ": " << e.what() << endl;
                            try { server->sendError(ErrorCode::EVALUATION_ERROR, e.what()); } catch (...) {}
                            session_ok = false;
                            break;
                        }
                    }
                }

                cout << "Session complete. Processed "
                     << nrof_fhe_queries << " FHE queries × " << nrof_rounds << " rounds." << endl;
                cout << "NET bytes_sent: " << server->getSentBytes() << endl;
                cout << "NET bytes_received: " << server->getReceivedBytes() << endl;
                server->resetByteCounters();

            } catch (const NetworkException& e) {
                cerr << "Outer network error: " << e.what() << endl;
            } catch (const exception& e) {
                cerr << "Outer error: " << e.what() << endl;
            }
        }

        cout << "\nShutdown complete. Processed " << query_count << " queries." << endl;
        return 0;

    } catch (const exception& e) {
        cerr << "Fatal error: " << e.what() << endl;
        return 1;
    }
}
