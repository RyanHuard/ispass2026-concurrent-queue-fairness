#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>
#include <ostream>
#include <papi.h>

enum class EventTimestamp {
  EnqueueInv = 0,   // when enqueue() is called
  EnqueueLin = 1, // when item enters the queue
  DequeueInv = 2,  // when item is dequeued
  DequeueLin = 3
};

struct OvertakeMetrics {
    double mean_all_elements = 0.0;   // avg overtake depth among all items
    double mean_overtaken_elements = 0.0; // avg overtake depth among items that got overtaken
    size_t max_depth = 0;// max depth of an overtake
    size_t count = 0;    // # items with depth > 0
    double pct = 0.0;    // 100 * count_overtaken / n
};

inline std::ostream& operator<<(std::ostream& os, const OvertakeMetrics& s) {
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

OvertakeMetrics compute_overtake_metrics(
  const std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>>&,
  EventTimestamp, EventTimestamp // the events being measured
);

inline OvertakeMetrics enqueue_fairness(const auto& records) { 
    return compute_overtake_metrics(records, EventTimestamp::EnqueueInv, EventTimestamp::EnqueueLin); 
}

inline OvertakeMetrics dequeue_fairness(const auto& records) { 
    return compute_overtake_metrics(records, EventTimestamp::DequeueInv, EventTimestamp::DequeueLin); 
}




#include <immintrin.h> 
#include <x86intrin.h>

static inline uint64_t now() {
    unsigned aux;
    uint64_t ts = __rdtscp(&aux);  
    _mm_lfence(); 
    return ts;
}

inline void wait(uint64_t ns = 1000) {
  using clock = std::chrono::steady_clock;
  const auto start = clock::now();
  while (std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - start).count() < ns) {}
}


static inline uint64_t get_field(
    const std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>& tup,
    EventTimestamp f)
{
    switch (f) {
        case EventTimestamp::EnqueueInv: return std::get<0>(tup);
        case EventTimestamp::EnqueueLin: return std::get<1>(tup);
        case EventTimestamp::DequeueInv: return std::get<2>(tup);
        case EventTimestamp::DequeueLin: return std::get<3>(tup);
    }
    return 0.0;
}

