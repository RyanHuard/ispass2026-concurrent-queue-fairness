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
    double mean = 0.0;   // avg overtake depth among items with depth > 0
    size_t max_depth = 0;// max depth of an overtake
    size_t count = 0;    // # items with depth > 0
    double pct = 0.0;    // 100 * count_overtaken / n
};

inline std::ostream& operator<<(std::ostream& os, const OvertakeDepthStats& s) {
    os << "mean=" << s.mean
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

inline uint64_t now() {
    return std::chrono::high_resolution_clock::now()
             .time_since_epoch()
             .count();
}

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
