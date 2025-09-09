#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>
#include <ostream>

#include "FairnessMetrics.hpp"


enum class EventTimestamp {
  EnqueueCall = 0,   // when enqueue() is called
  EnqueueInsert = 1, // when item enters the queue
  Dequeue = 3  // when item is dequeued
};

struct OvertakeDepthStats {
    double mean_all_elements = 0.0;   // avg overtake depth among all items
    double mean_overtaken_elements = 0.0; // avg overtake depth among items that got overtaken
    size_t max_depth = 0;// max depth of an overtake
    size_t count = 0;    // # items with depth > 0
    double pct = 0.0;    // 100 * count_overtaken / n
};

inline std::ostream& operator<<(std::ostream& os, const OvertakeDepthStats& s) {
    os << "mean_all=" << s.mean_all_elements
       << ", mean_ovt=" << s.mean_overtaken_elements
       << ", max=" << s.max_depth
       << ", count=" << s.count
       << ", pct=" << s.pct << "%";
    return os;
}

struct Record {
  double call_ts;
  double in_ts;
  double deq_ts;
};

class FairnessLogger {
public:
  FairnessLogger();
  
  void enable(FairnessMetric metric);
  void disable(FairnessMetric metric);
  bool is_enabled(FairnessMetric metric);
  
  void log_enqueue(double call_ts, double in_ts);
  void log_dequeue(double call_ts, double in_ts, double deq_ts);

  std::vector<Record> get_records();
  
private:
  mutable std::mutex mtx;
  std::vector<Record> records;
  std::vector<bool> enabled;
};

OvertakeDepthStats compute_overtake_metrics(
  const std::vector<std::tuple<uint64_t, uint64_t, uint64_t>>&,
  EventTimestamp, EventTimestamp // the events being measured
);

// Wrappers
inline OvertakeDepthStats insertion_fairness(
    const auto& records)
{ return compute_overtake_metrics(records, EventTimestamp::EnqueueCall, EventTimestamp::EnqueueInsert); }

inline OvertakeDepthStats service_fairness(
    const auto& records)
{ return compute_overtake_metrics(records, EventTimestamp::EnqueueInsert, EventTimestamp::Dequeue); }

inline OvertakeDepthStats end_to_end_fairness(
    const auto& records)
{ return compute_overtake_metrics(records, EventTimestamp::EnqueueCall, EventTimestamp::Dequeue); }

// inline uint64_t now() {
//     return std::chrono::high_resolution_clock::now()
//              .time_since_epoch()
//              .count();
// }

#include <immintrin.h>   // _mm_lfence
#include <x86intrin.h>   // __rdtscp, __rdtsc

// // Serializing timestamp read (preferred)
// static inline uint64_t now() {
//     _mm_lfence();
//     unsigned aux;
//     uint64_t t = __rdtscp(&aux);  // waits for all prior ops to complete
//     _mm_lfence();                 // keep subsequent ops after the read
//     return t;
// }

static inline uint64_t now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  return uint64_t(ts.tv_sec) * 1000000000ull + uint64_t(ts.tv_nsec);
}

#include <x86intrin.h>
#include <barrier>
#include <atomic>

static inline uint64_t rdtsc_now() {
    _mm_lfence();
    unsigned aux;
    uint64_t t = __rdtscp(&aux);   // serializing read
    _mm_lfence();                  // (optional) tighten ordering further
    return t;
}

inline thread_local int64_t TSC_OFFSET_CYC = 0;

static inline uint64_t adj_now() {
    _mm_lfence();
    unsigned aux;
    uint64_t t = __rdtscp(&aux);   // serializing read
    _mm_lfence();                  // (optional) tighten ordering further
    return t;
}

struct Calibrator {
  std::barrier<> sync;
  std::atomic<uint64_t> t0_cycles{0};

  explicit Calibrator(int n) : sync(n) {}

  // Call once per thread, early, with tid in [0..n-1]
  void calibrate(int tid) {
    sync.arrive_and_wait();

    if (tid == 0) t0_cycles.store(rdtsc_now(), std::memory_order_release);

    sync.arrive_and_wait();

    uint64_t t_ref = t0_cycles.load(std::memory_order_acquire);
    uint64_t t_local = rdtsc_now();
    TSC_OFFSET_CYC = (int64_t)t_ref - (int64_t)t_local;

    sync.arrive_and_wait();
  }
};




static inline uint64_t get_field(
    const std::tuple<uint64_t, uint64_t, uint64_t>& tup,
    EventTimestamp f)
{
    switch (f) {
        case EventTimestamp::EnqueueCall:   return std::get<0>(tup);
        case EventTimestamp::EnqueueInsert: return std::get<1>(tup);
        case EventTimestamp::Dequeue:       return std::get<2>(tup);
    }
    return 0.0; // unreachable
}


