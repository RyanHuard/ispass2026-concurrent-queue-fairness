
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


OvertakeDepthStats compute_overtake_metrics(
  const std::vector<std::tuple<uint64_t, uint64_t, uint64_t>>& records,
  EventTimestamp event1, EventTimestamp event2)
{
    /*
    Example overtake calculation: 
    E1: (1, 5)   // enq at t=1, deq at t=5
    E2: (2, 3)   // enq at t=2, deq at t=3

    1. Start at newest element (E2)
    2. min_deq = inf
    3. deq_ts > min_deq? (3 > inf)? No.
    4. set min_deq = 3.
    5. Go to next element (E1)
    6. deq_ts > min_deq? (5 > 3)? Yes.
    7. Element was overtaken, increment overtaken_count
    */

    OvertakeDepthStats result;
    if (records.empty()) return result;

    // (event1, event2) timestamps
    std::vector<std::pair<uint64_t, uint64_t>> ts_pairs;

    ts_pairs.reserve(records.size());

    for (const auto& record : records) {
      ts_pairs.emplace_back(get_field(record, event1), get_field(record, event2));
    }


    std::stable_sort(ts_pairs.begin(), ts_pairs.end(),
  [](const auto& a, const auto& b){
    if (a.first != b.first) return a.first < b.first;
    return a.second < b.second; // tie-break by dequeue time
  });


    std::vector<size_t> depth(ts_pairs.size(), 0);
    for (size_t i = 0; i < ts_pairs.size(); ++i) {
      for (size_t j = i + 1; j < ts_pairs.size(); ++j) {
        if (ts_pairs[j].first == ts_pairs[i].first) continue; // skip ties
        if (ts_pairs[j].second < ts_pairs[i].second && ts_pairs[j].first > ts_pairs[i].first ) ++depth[i];
      }
    }

    uint64_t sum = 0;
    for (auto d: depth) {
        sum += d;                          // add every depth, even 0
        if (d > result.max_depth) result.max_depth = d;
        if (d > 0) ++result.count;  // still track how many were overtaken
    }

    const double n = static_cast<double>(depth.size());
    result.mean = (double)sum / n;  // all items
    result.pct  = 100.0 * (double)result.count/ n;
    
    return result;
}



