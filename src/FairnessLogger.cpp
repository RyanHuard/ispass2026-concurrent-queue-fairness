
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

double compute_enq_to_deq_overtake_percentage(const std::vector<std::tuple<double, double, double>>& records) {
    if (records.empty()) return 0.0;

    /*
    Example: 
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

    // Extract (call_time, dequeue_time) pairs
    std::vector<std::pair<double, double>> times;
    times.reserve(records.size());
    for (const auto& e : records) {
        double call_ts = std::get<0>(e);
        double deq_ts  = std::get<1>(e);
        times.emplace_back(call_ts, deq_ts);
    }

    // Sort by call_time ascending
    std::sort(times.begin(), times.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    // Walk from end, track min dequeue time seen so far
    double min_deq = std::numeric_limits<double>::infinity();
    size_t overtaken_count = 0;

    for (int i = static_cast<int>(times.size()) - 1; i >= 0; --i) {
        double deq_ts = times[i].second;
        if (deq_ts > min_deq) {
            overtaken_count++;
        }
        min_deq = std::min(min_deq, deq_ts);
    }

    return 100.0 * overtaken_count / times.size();
}


// OvertakeDepthStats compute_enqueue_overtake_depth(
//     const std::vector<std::tuple<double, double, double>>& recs)
// {
//     if (recs.empty()) return {};

//     std::vector<std::pair<double, double>> t; 
//     t.reserve(recs.size());
    
//     for (auto& e: recs) { 
//       t.emplace_back(std::get<0>(e), std::get<1>(e)); 
//     }

//     sort(t.begin(), t.end(), [](auto& a, auto& b) { return a.first < b.first; });

//     std::vector<size_t> depth(t.size(), 0);
//     for (size_t i = 0; i < t.size(); ++i)
//         for (size_t j = i + 1; j < t.size(); ++j)
//             if (t[j].second < t[i].second) ++depth[i];

//     size_t mx = 0; 
//     unsigned long long sum = 0;

//     for (auto d: depth)
//     { 
//       mx = std::max(mx, d); 
//       sum += d; 
//     }

//     return { (double) sum / depth.size(), mx };
// }

OvertakeDepthStats compute_enqueue_overtake_depth(
    const std::vector<std::tuple<double,double,double>>& recs)
{
    OvertakeDepthStats out;
    if (recs.empty()) return out;

    // (call_ts, in_ts)
    std::vector<std::pair<double,double>> t;
    t.reserve(recs.size());
    for (auto& e: recs) t.emplace_back(std::get<0>(e), std::get<1>(e));
    std::sort(t.begin(), t.end(),
              [](auto& a, auto& b){ return a.first < b.first; });

    std::vector<size_t> depth(t.size(), 0);
    for (size_t i=0;i<t.size();++i)
        for (size_t j=i+1;j<t.size();++j)
            if (t[j].second < t[i].second) ++depth[i];

    unsigned long long sum = 0;
    for (auto d: depth) {
        sum += d;                          // add every depth, even 0
        out.max_depth = std::max(out.max_depth, d);
        if (d > 0) ++out.count_overtaken;  // still track how many were overtaken
    }

    if (!depth.empty()) {
        out.mean_overtaken = (double)sum / (double)depth.size();  // all items
        out.pct_overtaken  = 100.0 * (double)out.count_overtaken / depth.size();
    }
    return out;
}



