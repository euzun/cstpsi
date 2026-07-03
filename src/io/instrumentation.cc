// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
/**
 * Instrumentation implementation for CSTPSI performance profiling.
 *
 * Copyright 2026 - CSTPSI Publication-Ready Infrastructure
 */

#include "instrumentation.h"
#include <fstream>
#include <sstream>
#include <cstring>
#include <sys/resource.h>
#include <iostream>
#include <iomanip>

namespace cstpsi::instrumentation {

// ============================================================================
// Utility functions
// ============================================================================

double getRSSMB() {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
        return -1.0;
    }

    // On macOS, ru_maxrss is in bytes; on Linux, it's in KB
    #ifdef __APPLE__
    return ru.ru_maxrss / (1024.0 * 1024.0);
    #else
    return ru.ru_maxrss / 1024.0;
    #endif
}

// ============================================================================
// Timer implementation
// ============================================================================

Timer::Timer() : start_(high_resolution_clock::now()) {}

void Timer::reset() {
    start_ = high_resolution_clock::now();
}

int64_t Timer::elapsed_us() const {
    auto end = high_resolution_clock::now();
    return duration_cast<microseconds>(end - start_).count();
}

// ============================================================================
// Instrumentation implementation
// ============================================================================

void Instrumentation::recordOfflineStepStart(const std::string& step_name) {
    double rss_before = getRSSMB();
    rss_samples_[step_name + "_start"] = rss_before;
    step_timers_[step_name] = Timer();
}

void Instrumentation::recordOfflineStepEnd(const std::string& step_name) {
    int64_t elapsed = step_timers_[step_name].elapsed_us();
    double rss_after = getRSSMB();
    double rss_before = rss_samples_[step_name + "_start"];

    StepMetrics* target = nullptr;

    if (step_name == "step_init") {
        target = &session_.offline.step_init;
    } else if (step_name == "step_partition") {
        target = &session_.offline.step_partition;
    } else if (step_name == "step_blind") {
        target = &session_.offline.step_blind;
    } else if (step_name == "step_share") {
        target = &session_.offline.step_share;
    } else if (step_name == "step_interp_pack") {
        target = &session_.offline.step_interp_pack;
    }

    if (target) {
        // For steps with sub-steps, use the sum of sub-steps if available
        // Otherwise use the measured elapsed time
        if (step_name == "step_share") {
            auto& share_metrics = static_cast<StepShareMetrics&>(*target);
            if (share_metrics.sample_coef_us > 0 || share_metrics.eval_shares_us > 0) {
                share_metrics.wall_us = share_metrics.sample_coef_us + share_metrics.eval_shares_us;
            } else {
                share_metrics.wall_us = elapsed;
            }
        } else if (step_name == "step_interp_pack") {
            auto& interp_pack_metrics = static_cast<StepInterpPackMetrics&>(*target);
            if (interp_pack_metrics.interpolate_us > 0 || interp_pack_metrics.simd_pack_us > 0) {
                interp_pack_metrics.wall_us = interp_pack_metrics.interpolate_us + interp_pack_metrics.simd_pack_us;
            } else {
                interp_pack_metrics.wall_us = elapsed;
            }
        } else {
            target->wall_us = elapsed;
        }

        target->rss_delta_mb = rss_after - rss_before;
        target->rss_snapshot_mb = rss_after;
        session_.offline.s_peak_offline_rss_mb = std::max(
            session_.offline.s_peak_offline_rss_mb, rss_after);
    }

    rss_samples_[step_name + "_end"] = rss_after;
}

void Instrumentation::recordShareSubStepTiming(const std::string& sub_step_name, int64_t us) {
    if (sub_step_name == "sample_coef") {
        session_.offline.step_share.sample_coef_us = us;
    } else if (sub_step_name == "eval_shares") {
        session_.offline.step_share.eval_shares_us = us;
    }
}

void Instrumentation::recordInterpPackSubStepTiming(const std::string& sub_step_name, int64_t us) {
    if (sub_step_name == "interpolate") {
        session_.offline.step_interp_pack.interpolate_us = us;
    } else if (sub_step_name == "simd_pack") {
        session_.offline.step_interp_pack.simd_pack_us = us;
    }
}

void Instrumentation::recordInterpPackRSSAfterInterp(double rss_mb) {
    session_.offline.step_interp_pack.rss_after_interp_mb = rss_mb;
}

void Instrumentation::recordInterpPackRSSAfterPack(double rss_mb) {
    session_.offline.step_interp_pack.rss_after_pack_mb = rss_mb;
}

void Instrumentation::startQuery(int query_id, const std::string& query_type) {
    QueryMetrics qm;
    qm.query_id = query_id;
    qm.query_type = query_type;
    session_.queries.push_back(qm);
    current_query_id_ = query_id;
}

void Instrumentation::endQuery() {
    current_query_id_ = std::nullopt;
}

void Instrumentation::recordStepGC(int64_t gc_us, int64_t bytes_r_to_s, int64_t bytes_s_to_r,
                                    double r_rss_mb, double s_rss_mb) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    query.step_gc.gc_us = gc_us;
    query.step_gc.bytes_r_to_s = bytes_r_to_s;
    query.step_gc.bytes_s_to_r = bytes_s_to_r;
    query.step_gc.r_rss_mb = r_rss_mb;
    query.step_gc.s_rss_mb = s_rss_mb;
}

void Instrumentation::recordStepQueryPowers(int64_t r_encrypt_us, int64_t r_serialize_us,
                                            int64_t s_recv_us, int64_t cache_size_bytes,
                                            int64_t bytes_r_to_s, double r_rss_mb, double s_rss_mb) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    query.step_query_powers.r_encrypt_us = r_encrypt_us;
    query.step_query_powers.r_serialize_us = r_serialize_us;
    query.step_query_powers.s_recv_us = s_recv_us;
    query.step_query_powers.cache_size_bytes = cache_size_bytes;
    query.step_query_powers.bytes_r_to_s = bytes_r_to_s;
    query.step_query_powers.r_rss_mb = r_rss_mb;
    query.step_query_powers.s_rss_mb = s_rss_mb;
}

void Instrumentation::startHomRound(int t, const std::string& round_kind) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    HomRound round;
    round.t = t;
    round.round_kind = round_kind;
    query.step_hom_rounds.push_back(round);
    current_round_idx_ = (int)query.step_hom_rounds.size() - 1;
}

void Instrumentation::recordHomRoundSenderSubStep(const std::string& sub_step_name, int64_t us) {
    if (!current_query_id_.has_value() || !current_round_idx_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    auto& round = query.step_hom_rounds[current_round_idx_.value()];

    if (sub_step_name == "coef_load") {
        round.s.coef_load_us = us;
    } else if (sub_step_name == "ctxt_inner_product") {
        round.s.ctxt_inner_product_us = us;
    } else if (sub_step_name == "noise_flood") {
        round.s.noise_flood_us = us;
    } else if (sub_step_name == "mod_switch") {
        round.s.mod_switch_us = us;
    } else if (sub_step_name == "serialize") {
        round.s.serialize_us = us;
    } else if (sub_step_name == "wall") {
        round.s.wall_us = us;
    }
}

void Instrumentation::recordHomRoundReceiverSubStep(int64_t r_decrypt_us) {
    if (!current_query_id_.has_value() || !current_round_idx_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    auto& round = query.step_hom_rounds[current_round_idx_.value()];
    round.r.r_decrypt_us = r_decrypt_us;
}

void Instrumentation::recordHomRoundRSSAfterRerand(double rss_mb) {
    if (!current_query_id_.has_value() || !current_round_idx_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    auto& round = query.step_hom_rounds[current_round_idx_.value()];
    round.s.rss_after_rerand_mb = rss_mb;
}

void Instrumentation::recordHomRoundBytes(int64_t bytes_s_to_r) {
    if (!current_query_id_.has_value() || !current_round_idx_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    auto& round = query.step_hom_rounds[current_round_idx_.value()];
    round.bytes_s_to_r = bytes_s_to_r;
}

void Instrumentation::endHomRound() {
    current_round_idx_ = std::nullopt;
}

void Instrumentation::recordStepTokenCheck(int64_t r_us, int64_t pairs_tried, int64_t tokens_found) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    query.step_token_check.r_us = r_us;
    query.step_token_check.pairs_tried = pairs_tried;
    query.step_token_check.tokens_found = tokens_found;
}

void Instrumentation::recordStepLabelRecovery(int64_t r_us, int64_t labels_recovered) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    query.step_label_recovery.r_us = r_us;
    query.step_label_recovery.labels_recovered = labels_recovered;
}

void Instrumentation::recordQueryResult(bool matched, const std::vector<int>& labels) {
    if (!current_query_id_.has_value()) return;

    auto& query = session_.queries[current_query_id_.value()];
    query.matched = matched;
    query.labels = labels;
}

void Instrumentation::setSystemInfo(const json& hardware, const json& software) {
    session_.hardware = hardware;
    session_.software = software;
}

void Instrumentation::setConfig(const json& config) {
    session_.config = config;
}

void Instrumentation::setSeeds(int db_seed, int query_seed) {
    session_.seeds["db"] = db_seed;
    session_.seeds["query"] = query_seed;
}

json Instrumentation::toJSON() const {
    json j;

    // Use run_id from public member
    j["run_id"] = run_id;
    j["config"] = session_.config;
    j["seeds"] = session_.seeds;
    j["hardware"] = session_.hardware;
    j["software"] = session_.software;

    // Offline metrics
    json offline_j;
    auto to_step_json = [](const StepMetrics& m) {
        json sj;
        sj["wall_us"] = m.wall_us;
        sj["rss_delta_mb"] = m.rss_delta_mb;
        return sj;
    };

    offline_j["step_init"] = to_step_json(session_.offline.step_init);
    offline_j["step_partition"] = to_step_json(session_.offline.step_partition);
    offline_j["step_blind"] = to_step_json(session_.offline.step_blind);

    json step_share_j = to_step_json(session_.offline.step_share);
    step_share_j["sample_coef_us"] = session_.offline.step_share.sample_coef_us;
    step_share_j["eval_shares_us"] = session_.offline.step_share.eval_shares_us;
    offline_j["step_share"] = step_share_j;

    json step_interp_pack_j = to_step_json(session_.offline.step_interp_pack);
    step_interp_pack_j["interpolate_us"] = session_.offline.step_interp_pack.interpolate_us;
    step_interp_pack_j["simd_pack_us"] = session_.offline.step_interp_pack.simd_pack_us;
    step_interp_pack_j["rss_after_interp_mb"] = session_.offline.step_interp_pack.rss_after_interp_mb;
    step_interp_pack_j["rss_after_pack_mb"] = session_.offline.step_interp_pack.rss_after_pack_mb;
    offline_j["step_interp_pack"] = step_interp_pack_j;

    offline_j["total_offline_us"] = session_.offline.total_offline_us;
    offline_j["s_peak_offline_rss_mb"] = session_.offline.s_peak_offline_rss_mb;

    j["offline_once_per_session"] = offline_j;

    // Queries
    json queries_j = json::array();
    for (const auto& query : session_.queries) {
        json query_j;
        query_j["query_id"] = query.query_id;
        query_j["query_type"] = query.query_type;

        // step_gc
        json step_gc_j;
        step_gc_j["gc_us"] = query.step_gc.gc_us;
        step_gc_j["bytes_r_to_s"] = query.step_gc.bytes_r_to_s;
        step_gc_j["bytes_s_to_r"] = query.step_gc.bytes_s_to_r;
        step_gc_j["r_rss_mb"] = query.step_gc.r_rss_mb;
        step_gc_j["s_rss_mb"] = query.step_gc.s_rss_mb;
        query_j["step_gc"] = step_gc_j;

        // step_query_powers
        json step_qp_j;
        step_qp_j["r_encrypt_us"] = query.step_query_powers.r_encrypt_us;
        step_qp_j["r_serialize_us"] = query.step_query_powers.r_serialize_us;
        step_qp_j["s_recv_us"] = query.step_query_powers.s_recv_us;
        step_qp_j["cache_size_bytes"] = query.step_query_powers.cache_size_bytes;
        step_qp_j["bytes_r_to_s"] = query.step_query_powers.bytes_r_to_s;
        step_qp_j["r_rss_mb"] = query.step_query_powers.r_rss_mb;
        step_qp_j["s_rss_mb"] = query.step_query_powers.s_rss_mb;
        query_j["step_query_powers"] = step_qp_j;

        // step_hom_rounds
        json hom_rounds_j = json::array();
        for (const auto& round : query.step_hom_rounds) {
            json round_j;
            round_j["t"] = round.t;
            round_j["round_kind"] = round.round_kind;

            json s_j;
            s_j["coef_load_us"] = round.s.coef_load_us;
            s_j["ctxt_inner_product_us"] = round.s.ctxt_inner_product_us;
            s_j["noise_flood_us"] = round.s.noise_flood_us;
            s_j["mod_switch_us"] = round.s.mod_switch_us;
            s_j["serialize_us"] = round.s.serialize_us;
            s_j["wall_us"] = round.s.wall_us;
            s_j["rss_after_rerand_mb"] = round.s.rss_after_rerand_mb;
            round_j["s"] = s_j;

            json r_j;
            r_j["r_decrypt_us"] = round.r.r_decrypt_us;
            round_j["r"] = r_j;

            round_j["bytes_s_to_r"] = round.bytes_s_to_r;
            hom_rounds_j.push_back(round_j);
        }
        query_j["step_hom_rounds"] = hom_rounds_j;

        // step_token_check
        json step_tc_j;
        step_tc_j["r_us"] = query.step_token_check.r_us;
        step_tc_j["pairs_tried"] = query.step_token_check.pairs_tried;
        step_tc_j["tokens_found"] = query.step_token_check.tokens_found;
        query_j["step_token_check"] = step_tc_j;

        // step_label_recovery
        json step_lr_j;
        step_lr_j["r_us"] = query.step_label_recovery.r_us;
        step_lr_j["labels_recovered"] = query.step_label_recovery.labels_recovered;
        query_j["step_label_recovery"] = step_lr_j;

        // totals
        json totals_j;
        totals_j["online_wall_us"] = query.online_wall_us;
        totals_j["r_peak_rss_mb"] = query.r_peak_rss_mb;
        totals_j["s_peak_rss_mb"] = query.s_peak_rss_mb;
        totals_j["bytes_r_to_s_total"] = query.bytes_r_to_s_total;
        totals_j["bytes_s_to_r_total"] = query.bytes_s_to_r_total;
        query_j["totals"] = totals_j;

        // result
        json result_j;
        result_j["matched"] = query.matched;
        result_j["labels"] = query.labels;
        query_j["result"] = result_j;

        queries_j.push_back(query_j);
    }
    j["queries"] = queries_j;

    // session_totals
    json session_totals_j;
    session_totals_j["session_wall_us"] = session_.session_wall_us;
    session_totals_j["s_peak_rss_overall_mb"] = session_.s_peak_rss_overall_mb;
    j["session_totals"] = session_totals_j;

    return j;
}

void Instrumentation::finalize(const std::string& output_path) {
    // Compute final aggregates
    session_.offline.total_offline_us =
        session_.offline.step_init.wall_us +
        session_.offline.step_partition.wall_us +
        session_.offline.step_blind.wall_us +
        session_.offline.step_share.wall_us +
        session_.offline.step_interp_pack.wall_us;

    for (auto& query : session_.queries) {
        // Sum hom rounds and gc+qp
        int64_t online_total = query.step_gc.gc_us + query.step_query_powers.r_encrypt_us +
                               query.step_query_powers.r_serialize_us +
                               query.step_query_powers.s_recv_us +
                               query.step_token_check.r_us + query.step_label_recovery.r_us;

        for (const auto& round : query.step_hom_rounds) {
            online_total += round.s.wall_us + round.r.r_decrypt_us;
        }

        query.online_wall_us = online_total;

        // Sum bytes
        query.bytes_r_to_s_total = query.step_query_powers.bytes_r_to_s +
                                   query.step_gc.bytes_r_to_s;
        query.bytes_s_to_r_total = query.step_gc.bytes_s_to_r;
        for (const auto& round : query.step_hom_rounds) {
            query.bytes_s_to_r_total += round.bytes_s_to_r;
        }
    }

    // Session-level totals
    session_.session_wall_us = session_.offline.total_offline_us;
    for (const auto& query : session_.queries) {
        session_.session_wall_us += query.online_wall_us;
    }
    session_.s_peak_rss_overall_mb = session_.offline.s_peak_offline_rss_mb;
    for (const auto& query : session_.queries) {
        session_.s_peak_rss_overall_mb = std::max(
            session_.s_peak_rss_overall_mb, query.s_peak_rss_mb);
    }

    json j = toJSON();

    std::ofstream out(output_path);
    if (!out.is_open()) {
        std::cerr << "ERROR: Could not open output file: " << output_path << std::endl;
        return;
    }

    out << j.dump() << "\n";
    out.close();
}

} // namespace cstpsi::instrumentation
