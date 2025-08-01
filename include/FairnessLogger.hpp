#pragma once

#include <vector>
#include <mutex>
#include <unordered_map>

#include "FairnessMetrics.hpp"

struct Record {
  int tid;
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
  
  void log_enqueue(int tid, double call_ts, double in_ts);
   void log_dequeue(int tid, double call_ts, double in_ts, double deq_ts);

  std::vector<Record> get_logs();
  
private:
  mutable std::mutex mtx;
  std::vector<Record> records;
  std::vector<bool> enabled;
};


double compute_thread_fairness(const std::vector<std::tuple<int,double,double,double>>& records);

double compute_overtake_percentage(const std::vector<std::tuple<int, double, double, double>>& records);
