// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
// FLPSI soundness experiment harness.
// Measures FAR/FRR across 5 rungs on real LFW embeddings to show that the CSTPSI
// kernel restores the false-accept rate to the plaintext-matcher floor, while the
// STLPSI kernel (single token round) does not.
//
// CRITICAL design rule: per repeat we draw ONE set of LSH hyperplanes and ONE set of
// subsample masks, build EACH record once, and feed the SAME records to all 5 rungs,
// so the rungs are directly comparable. Rungs iii/iv/v all operate on the same 64-int
// subsample records; only the matching engine differs (k-of-N vs STLPSI vs CSTPSI).
//
// For FAR/FRR the FHE is irrelevant (BFV is exact), so iv/v use the protocol's
// plaintext polynomial evaluation (plain.cc) fed through the REAL reconstruction code
// (findMatches / findVerifiedPairs / reconstructLabels). The protocol-FAR (off-curve
// garbage accepts) lives in that reconstruction, not in the encryption.

#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <numeric>
#include <random>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <unordered_map>

#include "params.h"
#include "helper.h"
#include "receiver.h"
#include "sender.h"
#include "plain.h"
#include "modpoly.h"
#include "../src/flpsi/flpsi_wrapper.h"

using namespace std;
using namespace flpsi;

// Protocol globals defined in core.cc
extern I2SSM token_share_map;
extern I2SSM id_share_map;

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------
struct Embedding { int label; vector<float> data; };  // 128-d

// A built record (enrolled or query), all representations from the SAME masks/hp.
struct Record {
  int label;
  vector<float> mean_emb;        // L2-normalized mean embedding (rung i)
  vector<uint8_t> robust_bv;     // 256-bit robust template (rung ii)
  vector<uint64_t> sub;          // 64 subsample field elements (rungs iii/iv/v)
};

// Data loading is handled by flpsi::loadDataset() (sniffs LFWE | FPB1).

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static vector<float> normalizeVec(vector<float> v) {
  double s = 0; for (float x : v) s += (double)x * x;
  s = sqrt(s); if (s > 0) for (float& x : v) x = (float)(x / s);
  return v;
}
static vector<float> meanEmb(const vector<const Embedding*>& samples, int a, int b) {
  vector<float> m(samples[0]->data.size(), 0.0f);
  for (int i = a; i < b; ++i)
    for (size_t j = 0; j < m.size(); ++j) m[j] += samples[i]->data[j];
  for (float& x : m) x /= (b - a);
  return normalizeVec(m);
}
static double cosDist(const vector<float>& a, const vector<float>& b) {
  double d = 0; for (size_t i = 0; i < a.size(); ++i) d += (double)a[i] * b[i];
  return 1.0 - d;  // both L2-normalized
}
static int hammingDist(const vector<uint8_t>& a, const vector<uint8_t>& b) {
  int c = 0; for (size_t i = 0; i < a.size(); ++i) { uint8_t x = a[i] ^ b[i]; while (x) { c += x & 1; x >>= 1; } } return c;
}
static int agreeCount(const vector<uint64_t>& a, const vector<uint64_t>& b) {
  int c = 0; for (size_t i = 0; i < a.size(); ++i) c += (a[i] == b[i]); return c;
}

// Build a record from samples[a,b) using shared hyperplanes + masks.
static Record buildRecord(int label, const vector<const Embedding*>& samples, int a, int b,
                          const vector<vector<float>>& hp, const SubsampleMasks& masks, double ratio) {
  Record r; r.label = label;
  r.mean_emb = meanEmb(samples, a, b);
  vector<vector<uint8_t>> bvs;
  for (int i = a; i < b; ++i) bvs.push_back(lshEncode(samples[i]->data, hp));
  r.robust_bv = robustTemplate(bvs, ratio);
  r.sub = subsample(r.robust_bv, masks);
  return r;
}

// ---------------------------------------------------------------------------
// Rungs i/ii: threshold-swept 1:N verification -> EER
// genuine score = distance(query, OWN enrolled); impostor score = MIN distance over DB.
// accept if score < thr.  FRR = frac gen >= thr; FAR = frac imp < thr; EER where FAR=FRR.
// ---------------------------------------------------------------------------
static double eerFromScores(const vector<double>& gen, const vector<double>& imp) {
  vector<double> all; all.insert(all.end(), gen.begin(), gen.end()); all.insert(all.end(), imp.begin(), imp.end());
  sort(all.begin(), all.end());
  double bestGap = 1e9, bestThr = all.empty() ? 0 : all.front();
  for (double thr : all) {
    double frr = 0; for (double g : gen) frr += (g >= thr); frr /= max((size_t)1, gen.size());
    double far = 0; for (double i : imp) far += (i < thr);  far /= max((size_t)1, imp.size());
    if (fabs(far - frr) < bestGap) { bestGap = fabs(far - frr); bestThr = thr; }
  }
  double frr = 0; for (double g : gen) frr += (g >= bestThr); frr /= max((size_t)1, gen.size());
  double far = 0; for (double i : imp) far += (i < bestThr);  far /= max((size_t)1, imp.size());
  return 0.5 * (far + frr);
}

static double rung_cosine(const vector<Record>& db, const vector<Record>& gen, const vector<Record>& imp,
                          const unordered_map<int,int>& dbIndexByLabel) {
  vector<double> gscore, iscore;
  for (const auto& q : gen) {
    auto it = dbIndexByLabel.find(q.label);
    if (it == dbIndexByLabel.end()) continue;
    gscore.push_back(cosDist(q.mean_emb, db[it->second].mean_emb));
  }
  for (const auto& q : imp) {
    double mn = 1e9; for (const auto& d : db) mn = min(mn, cosDist(q.mean_emb, d.mean_emb));
    iscore.push_back(mn);
  }
  return eerFromScores(gscore, iscore);
}

static double rung_hamming(const vector<Record>& db, const vector<Record>& gen, const vector<Record>& imp,
                           const unordered_map<int,int>& dbIndexByLabel) {
  vector<double> gscore, iscore;
  for (const auto& q : gen) {
    auto it = dbIndexByLabel.find(q.label);
    if (it == dbIndexByLabel.end()) continue;
    gscore.push_back(hammingDist(q.robust_bv, db[it->second].robust_bv));
  }
  for (const auto& q : imp) {
    int mn = 1 << 30; for (const auto& d : db) mn = min(mn, hammingDist(q.robust_bv, d.robust_bv));
    iscore.push_back((double)mn);
  }
  return eerFromScores(gscore, iscore);
}

// ---------------------------------------------------------------------------
// Rung iii: plaintext k-of-N (the FMR floor). 1:N over the DB subsample sets.
// ---------------------------------------------------------------------------
static pair<double,double> rung_kofn(const vector<Record>& db, const vector<Record>& gen, const vector<Record>& imp,
                                     const unordered_map<int,int>& dbIndexByLabel,
                                     double& genMeanAgree, int& genMaxAgree) {
  int gmatch = 0; long long gsum = 0; genMaxAgree = 0;
  for (const auto& q : gen) {
    auto it = dbIndexByLabel.find(q.label);
    if (it == dbIndexByLabel.end()) continue;
    int ac = agreeCount(q.sub, db[it->second].sub);
    gsum += ac; genMaxAgree = max(genMaxAgree, ac);
    if (ac >= K_MATCH) gmatch++;
  }
  int imatch = 0;
  for (const auto& q : imp) {
    bool m = false;
    for (const auto& d : db) if (agreeCount(q.sub, d.sub) >= K_MATCH) { m = true; break; }
    if (m) imatch++;
  }
  genMeanAgree = gen.empty() ? 0 : (double)gsum / gen.size();
  double frr = gen.empty() ? 0 : 1.0 - (double)gmatch / gen.size();
  double far = imp.empty() ? 0 : (double)imatch / imp.size();
  return {far, frr};
}

// ---------------------------------------------------------------------------
// Kernel rungs iv/v: build the protocol DB ONCE, then evaluate each query via
// plaintext poly-eval + the real reconstruction code.
// ---------------------------------------------------------------------------
static void buildEnrMap(const vector<Record>& db, I2SSM& enr_ss_map) {
  enr_ss_map.clear();
  uint64_t mx = 0;
  for (const auto& r : db) { enr_ss_map[r.label] = r.sub; for (uint64_t v : r.sub) mx = max(mx, v); }
  MAX_SUB = mx;  // dummy padding will use values > MAX_SUB
}

// Rung iv: STLPSI (single token round). Expect FAR > plaintext floor (grows with DB).
static pair<double,double> rung_stlpsi(const vector<Record>& db, const vector<Record>& gen, const vector<Record>& imp) {
  I2SSM enr_ss_map; buildEnrMap(db, enr_ss_map);
  token = 0;
  token_share_map.clear(); id_share_map.clear();
  for (auto& kv : enr_ss_map) setShares(kv.first);
  I32Vector3D parts = parallelPartitionDB(enr_ss_map);
  UVector4D coeffs = computeCoefficients(parts, enr_ss_map, token_share_map, id_share_map);

  int gmatch = 0;
  for (const auto& q : gen) {
    vector<uint64_t> qs = q.sub;
    UVector2D qp = computeQueryPowers(qs);
    UVector3D res = plaintextPolyEval(qp, coeffs);
    vector<int> ids = findMatches(res);
    if (find(ids.begin(), ids.end(), q.label) != ids.end()) gmatch++;
  }
  int imatch = 0;
  for (const auto& q : imp) {
    vector<uint64_t> qs = q.sub;
    UVector2D qp = computeQueryPowers(qs);
    UVector3D res = plaintextPolyEval(qp, coeffs);
    if (!findMatches(res).empty()) imatch++;
  }
  double frr = gen.empty() ? 0 : 1.0 - (double)gmatch / gen.size();
  double far = imp.empty() ? 0 : (double)imatch / imp.size();
  return {far, frr};
}

// Rung v: CSTPSI (T token rounds). Partition once; T fresh-token coefficient sets once;
// per query: T plaintext evals -> findVerifiedPairs -> reconstructLabels. Expect FAR ~ floor.
static pair<double,double> rung_cstpsi(const vector<Record>& db, const vector<Record>& gen, const vector<Record>& imp, int T) {
  I2SSM enr_ss_map; buildEnrMap(db, enr_ss_map);
  I32Vector3D parts = parallelPartitionDB(enr_ss_map);
  vector<UVector4D> coeffs_rounds(T);
  for (int t = 0; t < T; ++t) {
    token = 0;  // secret is always 0; freshness comes from re-randomized share polynomials
    token_share_map.clear(); id_share_map.clear();
    for (auto& kv : enr_ss_map) setShares(kv.first);
    coeffs_rounds[t] = computeCoefficients(parts, enr_ss_map, token_share_map, id_share_map);
  }
  auto runQuery = [&](const vector<uint64_t>& sub) -> vector<int> {
    vector<uint64_t> qs = sub;
    UVector2D qp = computeQueryPowers(qs);
    vector<UVector3D> trr(T);
    for (int t = 0; t < T; ++t) trr[t] = plaintextPolyEval(qp, coeffs_rounds[t]);
    vector<PairSet> verified = findVerifiedPairs(trr);
    return reconstructLabels(verified, trr[0]);  // trr[0][1] is a valid id-channel sharing
  };
  int gmatch = 0;
  for (const auto& q : gen) { vector<int> ids = runQuery(q.sub); if (find(ids.begin(), ids.end(), q.label) != ids.end()) gmatch++; }
  int imatch = 0;
  for (const auto& q : imp) { if (!runQuery(q.sub).empty()) imatch++; }
  double frr = gen.empty() ? 0 : 1.0 - (double)gmatch / gen.size();
  double far = imp.empty() ? 0 : (double)imatch / imp.size();
  return {far, frr};
}

// ---------------------------------------------------------------------------
// Deep1B reproducibility manifest (plain text, human-readable). Saves, per
// repeat, the subsample masks and the selected enrolled/query ids so a run is
// auditable and reproducible. Reproducibility across rungs i..v is already
// guaranteed (one mask+selection per repeat, shared by all rungs, derived
// deterministically from seed+rep). --manifest-out writes it; --manifest-in
// re-derives the selection from the recorded seed and VERIFIES it matches the
// saved masks/ids (errors loudly on any drift). ENROLL is written as "ALL" when
// db_size == enroll_total (every enrollee selected).
// ---------------------------------------------------------------------------
struct RepeatSel {
  SubsampleMasks masks;
  vector<int> enrollIds;   // original Deep1B ids, in remapped-label order
  vector<int> queryIds;    // genuine query original ids, in order
  bool enrollAll = false;
};

static constexpr int MASK_HALF = SUBSAMPLE_BITS / 2;  // 7 bits per half

static void writeManifestHeader(ostream& o, const string& dataset, long long bytes,
                                int seed, int db_size, int repeats, int randomImpostors,
                                int Tcstpsi, int E, int Q) {
  o << "FLPSI_MANIFEST 1\n"
    << "dataset " << dataset << "\n"
    << "dataset_bytes " << bytes << "\n"
    << "mode BITS\n"
    << "seed " << seed << "\n"
    << "db_size " << db_size << "\n"
    << "repeats " << repeats << "\n"
    << "random_impostors " << randomImpostors << "\n"
    << "tcstpsi " << Tcstpsi << "\n"
    << "enroll_total " << E << "\n"
    << "query_total " << Q << "\n"
    << "L " << L << "\nN_SUB " << N_SUB << "\nK " << K_MATCH
    << "\nF " << F << "\npartition_size " << partition_size << "\n";
}

static void writeRepeatSel(ostream& o, int rep, const RepeatSel& s) {
  o << "REP " << rep << "\nMASKS";
  for (const auto& m : s.masks) { for (int x : m.first) o << " " << x; for (int x : m.second) o << " " << x; }
  o << "\n";
  if (s.enrollAll) o << "ENROLL ALL\n";
  else { o << "ENROLL " << s.enrollIds.size(); for (int id : s.enrollIds) o << " " << id; o << "\n"; }
  o << "QUERY " << s.queryIds.size(); for (int id : s.queryIds) o << " " << id; o << "\n";
}

struct ManifestCfg { int seed=0, db_size=0, repeats=0, randomImpostors=0, Tcstpsi=0;
                     int enroll_total=0, query_total=0, n_sub=0, Lbits=0, kmatch=0;
                     long long dataset_bytes=0; vector<RepeatSel> sel; };

static bool readManifest(const string& path, ManifestCfg& mc) {
  ifstream f(path);
  if (!f) { cerr << "cannot open manifest " << path << "\n"; return false; }
  string magic; int ver;
  if (!(f >> magic >> ver) || magic != "FLPSI_MANIFEST") { cerr << "bad manifest magic\n"; return false; }
  string key;
  while (f >> key && key != "REP") {
    if      (key == "dataset_bytes")    f >> mc.dataset_bytes;
    else if (key == "seed")             f >> mc.seed;
    else if (key == "db_size")          f >> mc.db_size;
    else if (key == "repeats")          f >> mc.repeats;
    else if (key == "random_impostors") f >> mc.randomImpostors;
    else if (key == "tcstpsi")          f >> mc.Tcstpsi;
    else if (key == "enroll_total")     f >> mc.enroll_total;
    else if (key == "query_total")      f >> mc.query_total;
    else if (key == "N_SUB")            f >> mc.n_sub;
    else if (key == "L")                f >> mc.Lbits;
    else if (key == "K")                f >> mc.kmatch;
    else { string v; f >> v; }  // skip dataset/mode/F/partition_size
  }
  // The masks loop below sizes by the compile-time constants; a manifest from a
  // binary with different protocol constants must NOT be silently mis-parsed.
  if (mc.n_sub  && mc.n_sub  != N_SUB)   { cerr << "manifest N_SUB " << mc.n_sub  << " != build " << N_SUB   << "\n"; return false; }
  if (mc.Lbits  && mc.Lbits  != L)       { cerr << "manifest L "     << mc.Lbits  << " != build " << L       << "\n"; return false; }
  if (mc.kmatch && mc.kmatch != K_MATCH) { cerr << "manifest K "     << mc.kmatch << " != build " << K_MATCH << "\n"; return false; }
  while (key == "REP") {
    int rep; f >> rep;
    if (rep != (int)mc.sel.size()) { cerr << "manifest REP index " << rep << " out of order (expected " << mc.sel.size() << ")\n"; return false; }
    RepeatSel s; string sec;
    f >> sec; if (sec != "MASKS") { cerr << "manifest: expected MASKS, got '" << sec << "'\n"; return false; }
    s.masks.resize(N_SUB);
    for (int i = 0; i < N_SUB; ++i) {
      s.masks[i].first.resize(MASK_HALF); s.masks[i].second.resize(MASK_HALF);
      for (int j = 0; j < MASK_HALF; ++j) f >> s.masks[i].first[j];
      for (int j = 0; j < MASK_HALF; ++j) f >> s.masks[i].second[j];
    }
    f >> sec; if (sec != "ENROLL") { cerr << "manifest: expected ENROLL, got '" << sec << "'\n"; return false; }
    string ev; f >> ev;
    if (ev == "ALL") s.enrollAll = true;
    else {
      if (ev.empty() || !all_of(ev.begin(), ev.end(), [](unsigned char c){ return std::isdigit(c); }))
        { cerr << "manifest: bad ENROLL count '" << ev << "'\n"; return false; }
      int cnt = stoi(ev); s.enrollIds.resize(cnt); for (int i = 0; i < cnt; ++i) f >> s.enrollIds[i];
    }
    f >> sec; if (sec != "QUERY") { cerr << "manifest: expected QUERY, got '" << sec << "'\n"; return false; }
    int qc; f >> qc; if (qc < 0) { cerr << "manifest: bad QUERY count\n"; return false; }
    s.queryIds.resize(qc); for (int i = 0; i < qc; ++i) f >> s.queryIds[i];
    if (!f) { cerr << "manifest parse error in REP block\n"; return false; }
    mc.sel.push_back(std::move(s));
    if (!(f >> key)) break;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Deep1B path: pre-made 256-bit codes (BITS). Rungs ii..v; rung i (cosine) is
// N/A (no float embeddings). Ground truth is the shared id (query id X matches
// enroll id X). FAR is the cross-id protocol-FAR: per query we COUNT non-partner
// accepts and split them into "real" (accept reconstructs a valid enrolled
// label) vs "garbage" (a value that is no enrolled label = protocol-added
// soundness FA). Synthetic random-bit queries (no partner) isolate the garbage
// accepts from the matcher FMR. NOTE: a garbage value can coincidentally fall in
// the label range [0, dbSize) (~dbSize/F) and be mis-binned as real; the random
// probe sidesteps this since it has no genuine signal.
// ---------------------------------------------------------------------------
static int runDeep1B(const Dataset& ds, const string& dataPath, int db_size, int repeats, int seed,
                     int Tcstpsi, int randomImpostors, const string& manifestOut, const string& manifestIn,
                     int maxGenQueries, const string& logQueries) {
  vector<int> enrollIds, queryIds;
  vector<const vector<uint8_t>*> enrollCode, queryCode;
  for (const auto& r : ds.records) {
    if (r.role == 0) { enrollIds.push_back(r.id); enrollCode.push_back(&r.code); }
    else             { queryIds.push_back(r.id);  queryCode.push_back(&r.code); }
  }
  const int E = (int)enrollIds.size(), Q = (int)queryIds.size();
  unordered_map<int,int> enrollIdx; enrollIdx.reserve(E * 2);
  for (int i = 0; i < E; ++i) enrollIdx[enrollIds[i]] = i;

  // --manifest-in: re-derive this run's config from a saved manifest and verify
  // the selection it recorded. --manifest-out: write the selection as we go.
  ManifestCfg mc; const bool verify = !manifestIn.empty();
  if (verify) {
    if (!readManifest(manifestIn, mc)) return 1;
    seed = mc.seed; db_size = mc.db_size; repeats = mc.repeats;
    randomImpostors = mc.randomImpostors; Tcstpsi = mc.Tcstpsi;
    cout << "manifest-in " << manifestIn << ": seed=" << seed << " db_size=" << db_size
         << " repeats=" << repeats << " random_impostors=" << randomImpostors << " T=" << Tcstpsi << "\n";
    // Catch a swapped dataset: the manifest's record counts and file size must match.
    if (mc.enroll_total && mc.enroll_total != E) { cerr << "manifest verify FAILED: enroll_total " << mc.enroll_total << " != dataset " << E << "\n"; return 1; }
    if (mc.query_total  && mc.query_total  != Q) { cerr << "manifest verify FAILED: query_total "  << mc.query_total  << " != dataset " << Q << "\n"; return 1; }
    long long actualBytes = 0; { ifstream df(dataPath, ios::binary | ios::ate); if (df) actualBytes = (long long)df.tellg(); }
    if (mc.dataset_bytes && actualBytes && mc.dataset_bytes != actualBytes) { cerr << "manifest verify FAILED: dataset_bytes " << mc.dataset_bytes << " != actual " << actualBytes << "\n"; return 1; }
  }
  if (db_size > E) db_size = E;
  const int dbSize = db_size;  // remapped labels are exactly 0..dbSize-1

  ofstream mfo;
  if (!manifestOut.empty()) {
    long long bytes = 0; { ifstream df(dataPath, ios::binary | ios::ate); if (df) bytes = (long long)df.tellg(); }
    mfo.open(manifestOut);
    if (!mfo) { cerr << "cannot open manifest-out " << manifestOut << "\n"; return 1; }
    writeManifestHeader(mfo, dataPath, bytes, seed, dbSize, repeats, randomImpostors, Tcstpsi, E, Q);
  }

  cout << "Deep1B BITS: enroll=" << E << " query=" << Q << " L=" << ds.dim_or_L
       << " db_size=" << dbSize << " repeats=" << repeats << " T=" << Tcstpsi
       << " random_impostors=" << randomImpostors << "\n";
  cout << "FAR = cross-id non-partner accepts per query (real=valid label, garbage=non-label)\n\n";

  // Per-query CSV log (raw per-query results, so nothing is lost to aggregation).
  ofstream qlog;
  if (!logQueries.empty()) {
    qlog.open(logQueries);
    if (qlog) { qlog << "rep,rung,qtype,qid,partner,real,garbage,hit\n"; cout << "logging per-query results to " << logQueries << "\n"; }
    else cerr << "warning: cannot open --log-queries " << logQueries << "\n";
  }

  double ii_eer = 0;
  double iii_frr=0, iii_genReal=0, iii_randReal=0;                       // iii: garbage=0 (plaintext)
  double iv_frr=0,  iv_genReal=0, iv_genGarb=0, iv_randReal=0, iv_randGarb=0;
  double v_frr=0,   v_genReal=0,  v_genGarb=0,  v_randReal=0,  v_randGarb=0;
  // correctness-error RATE = fraction of queries with >=1 garbage (protocol-added) accept
  double iv_genErr=0, iv_randErr=0, v_genErr=0, v_randErr=0;

  for (int rep = 0; rep < repeats; ++rep) {
    mt19937 rng(seed + rep);
    SubsampleMasks masks = makeSubsampleMasks(rng, seed + rep);

    // Genuine query set = first nGen of a shuffled query list; their enrollments
    // are guaranteed into the DB so every genuine query has a present partner.
    // Remaining DB slots are filled with random OTHER enroll ids.
    int nGen = min(dbSize, Q);
    if (maxGenQueries > 0) nGen = min(nGen, maxGenQueries);  // cap genuine queries (each is db-wide+heavy)
    vector<int> qOrder(Q); iota(qOrder.begin(), qOrder.end(), 0);
    shuffle(qOrder.begin(), qOrder.end(), rng);
    qOrder.resize(nGen);

    vector<int> dbEnrollIdx; dbEnrollIdx.reserve(dbSize);
    vector<char> chosen(E, 0);
    vector<int> genEi; genEi.reserve(qOrder.size());  // partner enroll index per genuine query
    for (int qi : qOrder) {
      auto it = enrollIdx.find(queryIds[qi]);
      if (it == enrollIdx.end()) { cerr << "runDeep1B: query id " << queryIds[qi] << " absent from enroll set\n"; return 1; }
      int ei = it->second; genEi.push_back(ei);
      if (!chosen[ei]) { chosen[ei]=1; dbEnrollIdx.push_back(ei); }
    }
    if ((int)dbEnrollIdx.size() != (int)qOrder.size()) { cerr << "runDeep1B: genuine query ids map to non-distinct enrollees\n"; return 1; }
    vector<int> eOrder(E); iota(eOrder.begin(), eOrder.end(), 0);
    shuffle(eOrder.begin(), eOrder.end(), rng);
    for (int ei : eOrder) { if ((int)dbEnrollIdx.size() >= dbSize) break; if (!chosen[ei]) { chosen[ei]=1; dbEnrollIdx.push_back(ei); } }

    // remap enroll index -> sequential label; build DB records
    unordered_map<int,int> labelOf; labelOf.reserve(dbEnrollIdx.size()*2);
    vector<Record> db; db.reserve(dbEnrollIdx.size());
    for (int lbl = 0; lbl < (int)dbEnrollIdx.size(); ++lbl) {
      int ei = dbEnrollIdx[lbl]; labelOf[ei] = lbl;
      Record r; r.label = lbl; r.robust_bv = *enrollCode[ei]; r.sub = subsample(*enrollCode[ei], masks);
      db.push_back(std::move(r));
    }
    const int curDbSize = (int)db.size();  // == dbSize for canonical data; the real/garbage boundary
    // genuine query records (label = partner's db index, since db is in label order)
    vector<Record> gen; gen.reserve(qOrder.size());
    for (size_t k = 0; k < qOrder.size(); ++k) {
      int ei = genEi[k];
      Record r; r.label = labelOf.at(ei); r.robust_bv = *queryCode[qOrder[k]]; r.sub = subsample(*queryCode[qOrder[k]], masks);
      gen.push_back(std::move(r));
    }

    // manifest: record (or verify) this repeat's masks + selected ids
    if (mfo.is_open() || verify) {
      RepeatSel cur; cur.masks = masks; cur.enrollAll = (dbSize == E);
      if (!cur.enrollAll) { cur.enrollIds.reserve(dbEnrollIdx.size()); for (int idx : dbEnrollIdx) cur.enrollIds.push_back(enrollIds[idx]); }
      cur.queryIds.reserve(qOrder.size()); for (int qi : qOrder) cur.queryIds.push_back(queryIds[qi]);
      if (mfo.is_open()) writeRepeatSel(mfo, rep, cur);
      if (verify) {
        if (rep >= (int)mc.sel.size()) { cerr << "manifest verify FAILED: fewer repeats recorded than requested\n"; return 1; }
        const RepeatSel& e = mc.sel[rep];
        bool ok = cur.masks == e.masks && cur.enrollAll == e.enrollAll
                  && (cur.enrollAll || cur.enrollIds == e.enrollIds) && cur.queryIds == e.queryIds;
        if (!ok) { cerr << "manifest verify FAILED at rep " << rep << ": selection drift vs " << manifestIn << "\n"; return 1; }
      }
    }

    // random-bit impostor queries (no enrolled partner)
    const int L_bytes = (ds.dim_or_L + 7) / 8;
    vector<Record> rnd; rnd.reserve(randomImpostors);
    uniform_int_distribution<int> byteDist(0, 255);
    for (int j = 0; j < randomImpostors; ++j) {
      vector<uint8_t> code(L_bytes);
      for (auto& b : code) b = (uint8_t)byteDist(rng);
      Record r; r.label = -1; r.robust_bv = code; r.sub = subsample(code, masks);
      rnd.push_back(std::move(r));
    }

    // Append this repeat's per-query rows for one rung to the CSV log (no-op if
    // --log-queries unset). Genuine rows carry the Deep1B query id + partner label.
    auto flushRows = [&](const char* rg,
                         const vector<double>& greal, const vector<double>& ggarb, const vector<char>& ghit,
                         const vector<double>& rreal, const vector<double>& rgarb) {
      if (!qlog.is_open()) return;
      for (size_t k = 0; k < greal.size(); ++k)
        qlog << rep << ',' << rg << ",gen," << queryIds[qOrder[k]] << ',' << gen[k].label
             << ',' << greal[k] << ',' << ggarb[k] << ',' << (int)ghit[k] << '\n';
      for (size_t k = 0; k < rreal.size(); ++k)
        qlog << rep << ',' << rg << ",rnd,-1,-1," << rreal[k] << ',' << rgarb[k] << ",0\n";
    };

    // Per-query loops below are parallelized over queries (reduction on the
    // counters). Counters are sums of integer increments stored as double, so
    // the reduction is exact regardless of thread order -> results are
    // deterministic. The inner plaintextPolyEval/findMatches pragmas nest under
    // these and serialize (nested OMP off by default), so no oversubscription.

    // ---- rung ii: Hamming EER (genuine vs random impostor) ----
    {
      vector<double> g(gen.size()), im(rnd.size());
      #pragma omp parallel for num_threads(nrof_online_threads)
      for (size_t k = 0; k < gen.size(); ++k)
        g[k] = hammingDist(gen[k].robust_bv, db[gen[k].label].robust_bv);
      #pragma omp parallel for num_threads(nrof_online_threads)
      for (size_t k = 0; k < rnd.size(); ++k) {
        int mn = 1<<30; for (const auto& d : db) mn = min(mn, hammingDist(rnd[k].robust_bv, d.robust_bv));
        im[k] = (double)mn;
      }
      ii_eer += eerFromScores(g, im);
    }

    // ---- rung iii: plaintext k-of-N (FMR floor; no reconstruction -> no garbage) ----
    {
      double genReal=0, randReal=0; int genHit=0;
      vector<double> g_real(gen.size(),0.0), r_real(rnd.size(),0.0); vector<char> g_hit(gen.size(),0);
      #pragma omp parallel for reduction(+:genReal,genHit) num_threads(nrof_online_threads)
      for (size_t k = 0; k < gen.size(); ++k) {
        const Record& q = gen[k]; bool hit=false; double gr=0;
        for (const auto& d : db) if (agreeCount(q.sub, d.sub) >= K_MATCH) { if (d.label==q.label) hit=true; else gr += 1.0; }
        g_real[k]=gr; g_hit[k]=(char)hit; genReal += gr; if (hit) genHit++;
      }
      #pragma omp parallel for reduction(+:randReal) num_threads(nrof_online_threads)
      for (size_t k = 0; k < rnd.size(); ++k) {
        double rr=0; for (const auto& d : db) if (agreeCount(rnd[k].sub, d.sub) >= K_MATCH) rr += 1.0;
        r_real[k]=rr; randReal += rr;
      }
      iii_frr      += gen.empty()?0:1.0-(double)genHit/gen.size();
      iii_genReal  += gen.empty()?0:genReal/gen.size();
      iii_randReal += rnd.empty()?0:randReal/rnd.size();
      flushRows("iii", g_real, vector<double>(gen.size(),0.0), g_hit, r_real, vector<double>(rnd.size(),0.0));
    }

    // ---- rung iv: STLPSI (single token round) ----
    {
      I2SSM enr; buildEnrMap(db, enr);
      token = 0; token_share_map.clear(); id_share_map.clear();
      for (auto& kv : enr) setShares(kv.first);
      I32Vector3D parts = parallelPartitionDB(enr);
      UVector4D coeffs = computeCoefficients(parts, enr, token_share_map, id_share_map);
      double genReal=0, genGarb=0, randReal=0, randGarb=0; int genHit=0, genErr=0, randErr=0;
      vector<double> g_real(gen.size(),0.0), g_garb(gen.size(),0.0), r_real(rnd.size(),0.0), r_garb(rnd.size(),0.0); vector<char> g_hit(gen.size(),0);
      #pragma omp parallel for reduction(+:genReal,genGarb,genHit,genErr) num_threads(nrof_online_threads)
      for (size_t k = 0; k < gen.size(); ++k) {
        vector<uint64_t> qs = gen[k].sub; UVector2D qp = computeQueryPowers(qs);
        UVector3D res = plaintextPolyEval(qp, coeffs);
        double fr=0, fg=0; bool hit=false;
        for (int v : findMatches(res)) { if (v==gen[k].label) hit=true; else if (v>=0 && v<curDbSize) fr+=1.0; else fg+=1.0; }
        g_real[k]=fr; g_garb[k]=fg; g_hit[k]=(char)hit; genReal+=fr; genGarb+=fg; if (hit) genHit++; if (fg>0) genErr++;
      }
      #pragma omp parallel for reduction(+:randReal,randGarb,randErr) num_threads(nrof_online_threads)
      for (size_t k = 0; k < rnd.size(); ++k) {
        vector<uint64_t> qs = rnd[k].sub; UVector2D qp = computeQueryPowers(qs);
        UVector3D res = plaintextPolyEval(qp, coeffs);
        double fr=0, fg=0;
        for (int v : findMatches(res)) { if (v>=0 && v<curDbSize) fr+=1.0; else fg+=1.0; }
        r_real[k]=fr; r_garb[k]=fg; randReal+=fr; randGarb+=fg; if (fg>0) randErr++;
      }
      iv_frr      += gen.empty()?0:1.0-(double)genHit/gen.size();
      iv_genReal  += gen.empty()?0:genReal/gen.size();   iv_genGarb  += gen.empty()?0:genGarb/gen.size();
      iv_randReal += rnd.empty()?0:randReal/rnd.size();  iv_randGarb += rnd.empty()?0:randGarb/rnd.size();
      iv_genErr   += gen.empty()?0:(double)genErr/gen.size();  iv_randErr += rnd.empty()?0:(double)randErr/rnd.size();
      flushRows("iv", g_real, g_garb, g_hit, r_real, r_garb);
    }

    // ---- rung v: CSTPSI (T token rounds) ----
    {
      I2SSM enr; buildEnrMap(db, enr);
      I32Vector3D parts = parallelPartitionDB(enr);
      vector<UVector4D> coeffs_rounds(Tcstpsi);
      for (int t=0;t<Tcstpsi;++t){ token=0; token_share_map.clear(); id_share_map.clear(); for (auto& kv: enr) setShares(kv.first); coeffs_rounds[t]=computeCoefficients(parts, enr, token_share_map, id_share_map); }
      auto runQuery = [&](const vector<uint64_t>& sub)->vector<int>{
        vector<uint64_t> qs = sub; UVector2D qp = computeQueryPowers(qs);
        vector<UVector3D> trr(Tcstpsi); for (int t=0;t<Tcstpsi;++t) trr[t]=plaintextPolyEval(qp, coeffs_rounds[t]);
        vector<PairSet> ver = findVerifiedPairs(trr); return reconstructLabels(ver, trr[0]);
      };
      double genReal=0, genGarb=0, randReal=0, randGarb=0; int genHit=0, genErr=0, randErr=0;
      vector<double> g_real(gen.size(),0.0), g_garb(gen.size(),0.0), r_real(rnd.size(),0.0), r_garb(rnd.size(),0.0); vector<char> g_hit(gen.size(),0);
      #pragma omp parallel for reduction(+:genReal,genGarb,genHit,genErr) num_threads(nrof_online_threads)
      for (size_t k = 0; k < gen.size(); ++k) {
        double fr=0, fg=0; bool hit=false;
        for (int v : runQuery(gen[k].sub)) { if (v==gen[k].label) hit=true; else if (v>=0 && v<curDbSize) fr+=1.0; else fg+=1.0; }
        g_real[k]=fr; g_garb[k]=fg; g_hit[k]=(char)hit; genReal+=fr; genGarb+=fg; if (hit) genHit++; if (fg>0) genErr++;
      }
      #pragma omp parallel for reduction(+:randReal,randGarb,randErr) num_threads(nrof_online_threads)
      for (size_t k = 0; k < rnd.size(); ++k) {
        double fr=0, fg=0;
        for (int v : runQuery(rnd[k].sub)) { if (v>=0 && v<curDbSize) fr+=1.0; else fg+=1.0; }
        r_real[k]=fr; r_garb[k]=fg; randReal+=fr; randGarb+=fg; if (fg>0) randErr++;
      }
      v_frr      += gen.empty()?0:1.0-(double)genHit/gen.size();
      v_genReal  += gen.empty()?0:genReal/gen.size();   v_genGarb  += gen.empty()?0:genGarb/gen.size();
      v_randReal += rnd.empty()?0:randReal/rnd.size();  v_randGarb += rnd.empty()?0:randGarb/rnd.size();
      v_genErr   += gen.empty()?0:(double)genErr/gen.size();  v_randErr += rnd.empty()?0:(double)randErr/rnd.size();
      flushRows("v", g_real, g_garb, g_hit, r_real, r_garb);
    }

    cout << "rep " << (rep+1) << "/" << repeats << "  db=" << db.size() << " gen=" << gen.size() << " rnd=" << rnd.size() << "\n";
  }

  double R = repeats;
  cout << "\n==================== DEEP1B AVERAGED (" << repeats << " repeats) ====================\n";
  cout << fixed << setprecision(4);
  cout << "(ii)  bitvectors EER = " << ii_eer/R << "\n";
  cout << "(iii) k-of-N  FRR=" << iii_frr/R << "  genuine FA/q real=" << iii_genReal/R << " garbage=0.0000  | random FA/q real=" << iii_randReal/R << " garbage=0.0000   [FMR floor]\n";
  cout << "(iv)  STLPSI  FRR=" << iv_frr/R  << "  genuine FA/q real=" << iv_genReal/R << " garbage=" << iv_genGarb/R << " | random FA/q real=" << iv_randReal/R << " garbage=" << iv_randGarb/R << "\n";
  cout << "        err-rate (frac queries with >=1 garbage accept): genuine=" << iv_genErr/R << " random=" << iv_randErr/R << "\n";
  cout << "(v)   CSTPSI  FRR=" << v_frr/R   << "  genuine FA/q real=" << v_genReal/R  << " garbage=" << v_genGarb/R  << " | random FA/q real=" << v_randReal/R  << " garbage=" << v_randGarb/R  << "\n";
  cout << "        err-rate (frac queries with >=1 garbage accept): genuine=" << v_genErr/R << " random=" << v_randErr/R << "\n";
  cout << "Expect: garbage ~0 for iii/v, grows with db for iv; random real ~0 for iii/v.\n";
  cout << "Hypothesis: STLPSI err-rate -> 1.0 (every query falsely accepts) as db grows; CSTPSI err-rate = 0.\n";
  if (mfo.is_open()) { mfo.flush(); if (!mfo) { cerr << "manifest write error to " << manifestOut << "\n"; return 1; } }
  return 0;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  int db_size = 1500, impostor_count = -1, repeats = 100, seed = 12345, Tcstpsi = 2;
  int randomImpostors = 1000;  // Deep1B only: synthetic random-bit queries (no enrolled partner)
  int threads = -1;            // --nrof-online-threads override; <0 => use the params default (cores-2)
  int maxGenQueries = -1;      // Deep1B only: cap genuine queries (<0 => all Q); each is db-wide+heavy
  double ratio = 0.9;
  string data = "experiments/data/lfw_embeddings.bin";
  string manifestOut, manifestIn;  // Deep1B reproducibility manifest (write / read+verify)
  string logQueries;               // Deep1B only: per-query CSV log (raw per-query results)
  for (int i = 1; i < argc; ++i) {
    string a = argv[i];
    if (a.rfind("--", 0) == 0 && i + 1 >= argc) { cerr << a << " requires an argument\n"; return 1; }
    if (a == "--db-size") db_size = stoi(argv[++i]);
    else if (a == "--impostors") impostor_count = stoi(argv[++i]);
    else if (a == "--repeats") repeats = stoi(argv[++i]);
    else if (a == "--seed") seed = stoi(argv[++i]);
    else if (a == "--ratio") ratio = stod(argv[++i]);
    else if (a == "--tcstpsi") Tcstpsi = stoi(argv[++i]);
    else if (a == "--random-impostors") randomImpostors = stoi(argv[++i]);
    else if (a == "--nrof-online-threads") threads = stoi(argv[++i]);
    else if (a == "--max-gen-queries") maxGenQueries = stoi(argv[++i]);
    else if (a == "--log-queries") logQueries = argv[++i];
    else if (a == "--manifest-out") manifestOut = argv[++i];
    else if (a == "--manifest-in") manifestIn = argv[++i];
    else if (a == "--data") data = argv[++i];
  }

  // Protocol globals
  inN = N_SUB; inK = K_MATCH; token = 0; FIELD_MODULUS = (long)F; N_CHANNELS = 2;
  if (threads < 0) threads = nrof_online_threads;  // params.cc default = (cores - 2)
  if (threads < 1) threads = 1;
  if (threads > 72) { cerr << "warning: --threads " << threads << " exceeds the modpoly _poly_vec bound (72); clamping to 72\n"; threads = 72; }
  cerr << "[config] online_threads=offline_threads=nrof_splits=" << threads
       << " (default = cores-2 from params; override with --nrof-online-threads)\n";
  // nrof_splits = threads: partitionDB fans out over splits (parallelPartitionDB's
  // omp loop is OVER splits), so this parallelizes the DB partitioning across cores.
  // Only the subject->partition grouping changes (statistically equivalent; affects
  // the already-stochastic off-curve garbage, not the deterministic FAR/FRR floor).
  partition_size = 32; nrof_splits = max(1, threads); m = N_SUB; nrof_online_threads = threads; nrof_offline_threads = threads;  // partition_size=32 matches demo_1k.json (N=64)
  ZERO = 0; ONE = 1;
  initializeSharePoly(FIELD_MODULUS);
  initializePolynomial(FIELD_MODULUS);

  Dataset ds;
  try { ds = loadDataset(data); }
  catch (const std::exception& e) { cerr << e.what() << "\n"; return 1; }

  // Deep1B (pre-made 256-bit codes) takes a dedicated path; the EMBED/LFW path
  // below is unchanged.
  if (ds.mode == DataMode::BITS)
    return runDeep1B(ds, data, db_size, repeats, seed, Tcstpsi, randomImpostors, manifestOut, manifestIn, maxGenQueries, logQueries);

  vector<Embedding> all;
  all.reserve(ds.records.size());
  for (auto& r : ds.records) all.push_back(Embedding{r.id, std::move(r.emb)});
  cout << "Loaded " << all.size() << " embeddings. db_size=" << db_size
       << " repeats=" << repeats << " ratio=" << ratio << " T_cstpsi=" << Tcstpsi << "\n";

  map<int, vector<const Embedding*>> byLabel;
  for (const auto& e : all) byLabel[e.label].push_back(&e);
  vector<int> genuineLabels, singletonLabels;
  for (auto& kv : byLabel) (kv.second.size() >= 2 ? genuineLabels : singletonLabels).push_back(kv.first);
  cout << "genuine-eligible people (>=2): " << genuineLabels.size()
       << " | singletons: " << singletonLabels.size() << "\n\n";

  double acc_i = 0, acc_ii = 0, far_iii = 0, frr_iii = 0, far_iv = 0, frr_iv = 0, far_v = 0, frr_v = 0;
  double acc_genMeanAgree = 0; int acc_genMaxAgree = 0;

  for (int rep = 0; rep < repeats; ++rep) {
    mt19937 rng(seed + rep);
    vector<vector<float>> hp(L, vector<float>(128));
    normal_distribution<float> nd(0, 1);
    for (int i = 0; i < L; ++i) { for (int j = 0; j < 128; ++j) hp[i][j] = nd(rng); hp[i] = normalizeVec(hp[i]); }
    SubsampleMasks masks = makeSubsampleMasks(rng, seed + rep);  // shared by ALL rungs this repeat

    vector<int> gl = genuineLabels;
    shuffle(gl.begin(), gl.end(), rng);
    if ((int)gl.size() > db_size) gl.resize(db_size);

    vector<Record> db, gen, imp;
    unordered_map<int,int> dbIndexByLabel;
    for (int lab : gl) {
      auto& s = byLabel[lab];
      int mid = (int)s.size() / 2;
      dbIndexByLabel[lab] = (int)db.size();
      db.push_back(buildRecord(lab, s, 0, mid, hp, masks, ratio));
      gen.push_back(buildRecord(lab, s, mid, (int)s.size(), hp, masks, ratio));
    }
    int icount = (impostor_count < 0) ? (int)gen.size() : impostor_count;
    icount = min(icount, (int)singletonLabels.size());
    vector<int> sl = singletonLabels; shuffle(sl.begin(), sl.end(), rng); sl.resize(icount);
    for (int lab : sl) imp.push_back(buildRecord(lab, byLabel[lab], 0, 1, hp, masks, ratio));

    acc_i  += rung_cosine(db, gen, imp, dbIndexByLabel);
    acc_ii += rung_hamming(db, gen, imp, dbIndexByLabel);
    double gMean; int gMax;
    auto riii = rung_kofn(db, gen, imp, dbIndexByLabel, gMean, gMax);
    far_iii += riii.first; frr_iii += riii.second; acc_genMeanAgree += gMean; acc_genMaxAgree = max(acc_genMaxAgree, gMax);
    auto riv = rung_stlpsi(db, gen, imp);
    far_iv += riv.first; frr_iv += riv.second;
    auto rv = rung_cstpsi(db, gen, imp, Tcstpsi);
    far_v += rv.first; frr_v += rv.second;

    cout << "rep " << (rep+1) << "/" << repeats << "  db=" << db.size() << " gen=" << gen.size() << " imp=" << imp.size()
         << " | iii far=" << riii.first << " frr=" << riii.second
         << " | iv far=" << riv.first << " frr=" << riv.second
         << " | v far=" << rv.first << " frr=" << rv.second
         << " | genAgree mean=" << gMean << " max=" << gMax << "\n";
  }

  double R = repeats;
  cout << "\n==================== AVERAGED RESULTS (" << repeats << " repeats) ====================\n";
  cout << fixed << setprecision(4);
  cout << "(i)   embeddings   EER = " << acc_i / R << "\n";
  cout << "(ii)  bitvectors   EER = " << acc_ii / R << "\n";
  cout << "(iii) k-of-N       FAR = " << far_iii / R << "  FRR = " << frr_iii / R << "   [FMR floor]\n";
  cout << "(iv)  STLPSI T=1   FAR = " << far_iv  / R << "  FRR = " << frr_iv  / R << "\n";
  cout << "(v)   CSTPSI T=" << Tcstpsi << "   FAR = " << far_v   / R << "  FRR = " << frr_v   / R << "\n";
  cout << "genuine own-agreement: mean = " << acc_genMeanAgree / R << "  max(seen) = " << acc_genMaxAgree
       << "  (need >= " << K_MATCH << " to match)\n";
  return 0;
}
