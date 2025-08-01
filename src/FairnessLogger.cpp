
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

/*inline void FairnessLogger::log_dequeue(int tid, double call_ts, double in_ts, double deq_ts) {
  std::lock_guard<std::mutex> lock(mtx);
  records.emplace_back(Record{tid, call_ts, in_ts, deq_ts});
  }*/

std::vector<Record> FairnessLogger::get_logs() { return records; }

#include <cmath>
#include <numeric>
double compute_thread_fairness(const std::vector<std::tuple<int,double,double,double>>& records) {
    if (records.empty()) return 0.5;

    std::unordered_map<int, std::pair<double, size_t>> sums;
    for (const auto& r : records) {
        int tid;
        double call_ts, in_ts, deq_ts;
        std::tie(tid, call_ts, in_ts, deq_ts) = r;

        if (deq_ts > call_ts) {
            double wait = deq_ts - call_ts;
            auto& e = sums[tid];
            e.first  += wait;
            e.second += 1;
        }
    }

    if (sums.empty()) return 1.0;

    std::vector<double> avg;
    avg.reserve(sums.size());
    for (auto& kv : sums)
        avg.push_back(kv.second.first / kv.second.second);

    double mean = std::accumulate(avg.begin(), avg.end(), 0.0) / avg.size();
    double var = 0.0;
    for (double v : avg) var += (v - mean) * (v - mean);
    var /= avg.size();
    double stddev = std::sqrt(var);

    return (mean > 0.0) ? 1.0 - (stddev / mean) : 1.0;
}

#include <algorithm>

double compute_overtake_percentage(const std::vector<std::tuple<int, double, double, double>>& records) {
    if (records.empty()) return 0.0;

    // Extract (call_time, dequeue_time) pairs
    std::vector<std::pair<double, double>> times;
    times.reserve(records.size());
    for (const auto& e : records) {
        double call_t = std::get<1>(e);
        double deq_t  = std::get<3>(e);
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

