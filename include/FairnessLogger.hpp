#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>

#include "FairnessMetrics.hpp"

// struct OvertakeDepthStats { 
//   double mean = 0; 
//   size_t max = 0; 
// };

struct OvertakeDepthStats {
    double mean_overtaken = 0.0;   // avg depth among items with depth>0
    size_t max_depth = 0;          // max depth
    size_t count_overtaken = 0;    // # items with depth>0
    double pct_overtaken = 0.0;    // 100 * count_overtaken / n
};

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


double compute_enq_to_deq_overtake_percentage(const std::vector<std::tuple<double, double, double>>& records);

OvertakeDepthStats compute_enqueue_overtake_depth(const std::vector<std::tuple<double,double,double>>& recs);

inline double now() {
    return std::chrono::high_resolution_clock::now()
             .time_since_epoch()
             .count();
  }
