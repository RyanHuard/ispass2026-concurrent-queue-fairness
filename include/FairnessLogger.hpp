#pragma once

#include <vector>
#include <mutex>

#include "FairnessMetric.hpp"

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
  void log_dequeue(int tid, double deq_ts);
  
private:
  mutable std::mutex mtx;
  std::vector<Record> records;
  std::vector<bool> enabled;
};
