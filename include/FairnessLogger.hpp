#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>

#include "FairnessMetrics.hpp"

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


double compute_overtake_percentage(const std::vector<std::tuple<double, double, double>>& records);

inline double now() {
    return std::chrono::high_resolution_clock::now()
             .time_since_epoch()
             .count();
  }
