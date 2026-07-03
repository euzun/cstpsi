// CSTPSI -- Composable Set-Threshold Labeled PSI
// Author: Erkam Uzun
// Copyright (c) 2026 Erkam Uzun. PolyForm Noncommercial License 1.0.0.
//
#include "params.h"
#include <thread>

// Default thread count for online phase = (logical cores - 2), leaving headroom for the OS.
// Overridable via the param config (param_loader) or the binary's --nrof-online-threads flag.
static int default_threads() {
  int hc = (int)std::thread::hardware_concurrency();
  return hc > 2 ? hc - 2 : 1;
}

// All available cores for offline preprocessing (runs alone, can use every core).
// Overridable via the param config or the binary's --nrof-offline-threads flag.
static int all_cores() {
  int hc = (int)std::thread::hardware_concurrency();
  return hc > 0 ? hc : 1;
}

// Configurable (read from JSON in param_loader.cc)
int nrof_que_ids;
int inN;
int m;
int partition_size;
int nrof_splits;
int nrof_collisions;
// Derived
int nrof_enr_ids;
int nrof_enr_total = 0;
uint64_t MAX_SUB = -1;
// Threshold + runtime
int inK;
int nrof_online_threads = default_threads();   // online phase; default (cores - 2)
int nrof_offline_threads = all_cores();        // offline preprocessing (partition + coeff encode); default all cores
long FIELD_MODULUS = 8519681;
int token = 0;
// Utility
int ZERO = 0;
int ONE = 1;
int N_CHANNELS = 2;  // Number of channels (token + label)
