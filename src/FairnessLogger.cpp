
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

void FairnessLogger::log_enqueue(double call_ts, double in_ts) {
  std::lock_guard<std::mutex> lock(mtx);
  records.push_back(Record{call_ts, in_ts, 0});
}

/*inline void FairnessLogger::log_dequeue(double call_ts, double in_ts, double deq_ts) {
  std::lock_guard<std::mutex> lock(mtx);
  records.emplace_back(Record{call_ts, in_ts, deq_ts});
  }*/

std::vector<Record> FairnessLogger::get_records() { return records; }

#include <cmath>
#include <numeric>
#include <algorithm>

double compute_overtake_percentage(const std::vector<std::tuple<double, double, double>>& records) {
    if (records.empty()) return 0.0;

    // Extract (call_time, dequeue_time) pairs
    std::vector<std::pair<double, double>> times;
    times.reserve(records.size());
    for (const auto& e : records) {
        double call_t = std::get<0>(e);
        double deq_t  = std::get<2>(e);
        times.emplace_back(call_t, deq_t);
    }

    // Sort by call_time ascending
    std::sort(times.begin(), times.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Walk from end, track min dequeue time seen so far
    double min_deq = std::numeric_limits<double>::infinity();
    size_t overtaken_count = 0;

    for (int i = static_cast<int>(times.size()) - 1; i >= 0; --i) {
        double deq_t = times[i].second;
        if (deq_t > min_deq) {
            ++overtaken_count;
        }
        min_deq = std::min(min_deq, deq_t);
    }

    return 100.0 * overtaken_count / times.size();
}

