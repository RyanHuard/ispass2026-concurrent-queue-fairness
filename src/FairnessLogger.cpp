#include "FairnessLogger.hpp"

FairnessLogger::FairnessLogger()
  : enabled(3, true) {}

void FairnessLogger::enable(FairnessMetric metric) {
  enabled[static_cast<size_t>(metric)] = true;
}

void FairnessLogger::disable(FairnessMetric metric) {
  enabled[static_cast<size_t>(metric)] = false;
}

bool FairnessLogger::is_enabled(FairnessMetric metric) {
  return enabled[static_cast<size_t>(metric)];
}

void FairnessLogger::log_enqueue(int tid, double call_ts, double in_ts) {
  std::lock_guard<std::mutex> lock(mtx);
  records.push_back(Record{tid, call_ts, in_ts, 0});
}

void FairnessLogger::log_dequeue(int tid, double deq_ts) {
  std::lock_guard<std::mutex> lock(mtx);
}
  
