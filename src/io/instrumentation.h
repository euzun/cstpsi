// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Instrumentation utilities for CSTPSI fine-grained performance profiling.
 *
 * Provides timing, RSS sampling, byte-counting, and JSONL output for
 * per-step and sub-step measurements across the protocol pipeline.
 *
 * Copyright 2026 - CSTPSI Publication-Ready Infrastructure
 */

#ifndef CSTPSI_INSTRUMENTATION_H
#define CSTPSI_INSTRUMENTATION_H

#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace std::chrono;

namespace cstpsi::instrumentation {

/**
 * Get current resident set size (RSS) in MB.
 * Returns -1 if unable to determine.
 */
double getRSSMB();

/**
 * High-resolution timer returning elapsed microseconds.
 */
class Timer {
public:
    Timer();
    void reset();
    int64_t elapsed_us() const;

private:
    high_resolution_clock::time_point start_;
};

/**
 * Tracks timing and memory for a single step/sub-step.
 */
struct StepMetrics {
    int64_t wall_us = 0;          // Total wall-clock time in microseconds
    double rss_delta_mb = 0.0;    // RSS delta since last measurement
    double rss_snapshot_mb = 0.0; // Current RSS snapshot
};

/**
 * Tracks timing for sub-steps within step_share.
 */
struct StepShareMetrics : public StepMetrics {
    int64_t sample_coef_us = 0;
    int64_t eval_shares_us = 0;
};

/**
 * Tracks timing for sub-steps within step_interp_pack.
 */
struct StepInterpPackMetrics : public StepMetrics {
    int64_t interpolate_us = 0;
    int64_t simd_pack_us = 0;
    double rss_after_interp_mb = 0.0;
    double rss_after_pack_mb = 0.0;
};

/**
 * Tracks per-round sender-side homomorphic evaluation metrics.
 */
struct HomRoundSenderMetrics {
    int64_t coef_load_us = 0;
    int64_t ctxt_inner_product_us = 0;
    int64_t noise_flood_us = 0;
    int64_t mod_switch_us = 0;
    int64_t serialize_us = 0;
    int64_t wall_us = 0;
    double rss_after_rerand_mb = 0.0;
};

/**
 * Tracks per-round receiver-side homomorphic evaluation metrics.
 */
struct HomRoundReceiverMetrics {
    int64_t r_decrypt_us = 0;
};

/**
 * Tracks a single homomorphic evaluation round.
 */
struct HomRound {
    int t = 0;                                    // Round number
    std::string round_kind = "token";             // "token" or "label"
    HomRoundSenderMetrics s;
    HomRoundReceiverMetrics r;
    int64_t bytes_s_to_r = 0;                     // Bytes sent sender -> receiver
};

/**
 * Tracks step_gc metrics (1-GC garbled circuit).
 */
struct StepGCMetrics {
    int64_t gc_us = 0;
    int64_t bytes_r_to_s = 0;
    int64_t bytes_s_to_r = 0;
    double r_rss_mb = 0.0;
    double s_rss_mb = 0.0;
};

/**
 * Tracks step_query_powers metrics.
 */
struct StepQueryPowersMetrics {
    int64_t r_encrypt_us = 0;
    int64_t r_serialize_us = 0;
    int64_t s_recv_us = 0;
    int64_t cache_size_bytes = 0;
    int64_t bytes_r_to_s = 0;
    double r_rss_mb = 0.0;
    double s_rss_mb = 0.0;
};

/**
 * Tracks step_token_check metrics.
 */
struct StepTokenCheckMetrics {
    int64_t r_us = 0;
    int64_t pairs_tried = 0;
    int64_t tokens_found = 0;
};

/**
 * Tracks step_label_recovery metrics.
 */
struct StepLabelRecoveryMetrics {
    int64_t r_us = 0;
    int64_t labels_recovered = 0;
};

/**
 * Offline-phase metrics (collected once per session).
 */
struct OfflineMetrics {
    StepMetrics step_init;
    StepMetrics step_partition;
    StepMetrics step_blind;
    StepShareMetrics step_share;
    StepInterpPackMetrics step_interp_pack;
    int64_t total_offline_us = 0;
    double s_peak_offline_rss_mb = 0.0;
};

/**
 * Per-query online-phase metrics.
 */
struct QueryMetrics {
    int query_id = 0;
    std::string query_type = "TN";
    StepGCMetrics step_gc;
    StepQueryPowersMetrics step_query_powers;
    std::vector<HomRound> step_hom_rounds;
    StepTokenCheckMetrics step_token_check;
    StepLabelRecoveryMetrics step_label_recovery;

    // Totals
    int64_t online_wall_us = 0;
    double r_peak_rss_mb = 0.0;
    double s_peak_rss_mb = 0.0;
    int64_t bytes_r_to_s_total = 0;
    int64_t bytes_s_to_r_total = 0;

    // Result
    bool matched = false;
    std::vector<int> labels;
};

/**
 * Session-level metrics.
 */
struct SessionMetrics {
    std::string run_id;
    json config;
    json seeds;
    json hardware;
    json software;
    OfflineMetrics offline;
    std::vector<QueryMetrics> queries;
    int64_t session_wall_us = 0;
    double s_peak_rss_overall_mb = 0.0;
};

/**
 * Main instrumentation recorder.
 *
 * Usage:
 *   Instrumentation inst;
 *   inst.setConfig(config_json);
 *   inst.setSeeds(db_seed, query_seed);
 *   inst.run_id = generateUUID();
 *   inst.recordOfflineStepStart("step_init");
 *   // ... do work ...
 *   inst.recordOfflineStepEnd("step_init");
 *   // ... repeat for each step ...
 *   inst.finalize("/path/to/output.jsonl");
 */
class Instrumentation {
public:
    std::string run_id;  // Set by caller before finalize()
    /**
     * Record the start of an offline step.
     * Captures current time and RSS.
     */
    void recordOfflineStepStart(const std::string& step_name);

    /**
     * Record the end of an offline step.
     * Computes elapsed time and RSS delta.
     */
    void recordOfflineStepEnd(const std::string& step_name);

    /**
     * Record sub-step timing within step_share.
     */
    void recordShareSubStepTiming(const std::string& sub_step_name, int64_t us);

    /**
     * Record sub-step timing within step_interp_pack.
     */
    void recordInterpPackSubStepTiming(const std::string& sub_step_name, int64_t us);

    /**
     * Record RSS snapshot after interpolation (step_interp_pack).
     */
    void recordInterpPackRSSAfterInterp(double rss_mb);

    /**
     * Record RSS snapshot after packing (step_interp_pack).
     */
    void recordInterpPackRSSAfterPack(double rss_mb);

    /**
     * Start recording metrics for a new query (online phase).
     */
    void startQuery(int query_id, const std::string& query_type = "TN");

    /**
     * End current query metrics.
     */
    void endQuery();

    /**
     * Record step_gc metrics.
     */
    void recordStepGC(int64_t gc_us, int64_t bytes_r_to_s, int64_t bytes_s_to_r,
                      double r_rss_mb, double s_rss_mb);

    /**
     * Record step_query_powers metrics.
     */
    void recordStepQueryPowers(int64_t r_encrypt_us, int64_t r_serialize_us,
                               int64_t s_recv_us, int64_t cache_size_bytes,
                               int64_t bytes_r_to_s, double r_rss_mb, double s_rss_mb);

    /**
     * Start recording a homomorphic evaluation round.
     */
    void startHomRound(int t, const std::string& round_kind);

    /**
     * Record sub-step timing within a homomorphic round (sender side).
     */
    void recordHomRoundSenderSubStep(const std::string& sub_step_name, int64_t us);

    /**
     * Record sub-step timing within a homomorphic round (receiver side).
     */
    void recordHomRoundReceiverSubStep(int64_t r_decrypt_us);

    /**
     * Record RSS snapshot after rerandomization.
     */
    void recordHomRoundRSSAfterRerand(double rss_mb);

    /**
     * Record bytes transmitted in current hom round.
     */
    void recordHomRoundBytes(int64_t bytes_s_to_r);

    /**
     * Finalize current homomorphic round.
     */
    void endHomRound();

    /**
     * Record step_token_check metrics.
     */
    void recordStepTokenCheck(int64_t r_us, int64_t pairs_tried, int64_t tokens_found);

    /**
     * Record step_label_recovery metrics.
     */
    void recordStepLabelRecovery(int64_t r_us, int64_t labels_recovered);

    /**
     * Record final query result.
     */
    void recordQueryResult(bool matched, const std::vector<int>& labels);

    /**
     * Finalize session and write JSONL.
     */
    void finalize(const std::string& output_path);

    /**
     * Set hardware/software info (call once before offline phase).
     */
    void setSystemInfo(const json& hardware, const json& software);

    /**
     * Set run configuration (call once at start).
     */
    void setConfig(const json& config);

    /**
     * Set DB/query seeds (call once at start).
     */
    void setSeeds(int db_seed, int query_seed);

private:
    SessionMetrics session_;
    std::optional<int> current_query_id_;
    std::optional<int> current_round_idx_;
    std::map<std::string, Timer> step_timers_;
    std::map<std::string, double> rss_samples_;

    json toJSON() const;
};

} // namespace cstpsi::instrumentation

#endif // CSTPSI_INSTRUMENTATION_H
