#include <cmath>
#include <numeric>
#include <algorithm>

#include "FairnessLogger.hpp"


// Fenwick (Binary Indexed) Tree for prefix counts
struct Fenwick {
    std::vector<int> bit;
    explicit Fenwick(int n) : bit(n + 1, 0) {}
    void add(int idx, int val) { for (; idx < (int)bit.size(); idx += idx & -idx) bit[idx] += val; }
    int sum_prefix(int idx) const { int s = 0; for (; idx > 0; idx -= idx & -idx) s += bit[idx]; return s; }
};

OvertakeMetrics compute_overtake_metrics(
    const std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>>& records,
    EventTimestamp event1, EventTimestamp event2)
{
    OvertakeMetrics res;
    if (records.empty()) return res;

    // Build (e1,e2), skipping e1==0 (e.g., in_ts==0)
    std::vector<std::pair<uint64_t,uint64_t>> ts_pairs;
    ts_pairs.reserve(records.size());
    for (const auto& rec : records) {
        const uint64_t e1 = get_field(rec, event1);
        if (e1 == 0) continue;                 // <-- skip zero in_ts
        const uint64_t e2 = get_field(rec, event2);
        ts_pairs.emplace_back(e1, e2);
    }
    if (ts_pairs.empty()) return res;

    // Sort by event1 asc, then event2 asc (tie-break doesn’t affect correctness)
    std::stable_sort(ts_pairs.begin(), ts_pairs.end(),
        [](const auto& a, const auto& b){
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

    // Coordinate-compress event2 to [1..M]
    std::vector<uint64_t> e2_vals;
    e2_vals.reserve(ts_pairs.size());
    for (auto& p : ts_pairs) e2_vals.push_back(p.second);
    std::sort(e2_vals.begin(), e2_vals.end());
    e2_vals.erase(std::unique(e2_vals.begin(), e2_vals.end()), e2_vals.end());
    auto rank_e2 = [&](uint64_t v){
        return (int)(std::lower_bound(e2_vals.begin(), e2_vals.end(), v) - e2_vals.begin()) + 1; // 1-based
    };

    // Sweep from largest event1 to smallest.
    // Process equal-e1 items as a group: query first, then insert all from the group.
    const int M = (int)e2_vals.size();
    Fenwick bit(M);

    std::vector<std::size_t> depth(ts_pairs.size(), 0);

    std::size_t i = ts_pairs.size();
    while (i > 0) {
        std::size_t j = i;
        const uint64_t e1 = ts_pairs[i-1].first;
        // group is [k..i-1] with same e1
        while (j > 0 && ts_pairs[j-1].first == e1) --j;

        // Query depths for this group (tree has only strictly larger event1s)
        for (std::size_t k = j; k < i; ++k) {
            int r = rank_e2(ts_pairs[k].second);
            // strict < on event2 ⇒ use prefix up to r-1
            depth[k] = (r > 1) ? bit.sum_prefix(r - 1) : 0;
        }

        // Insert this group's e2 into the tree
        for (std::size_t k = j; k < i; ++k) {
            int r = rank_e2(ts_pairs[k].second);
            bit.add(r, 1);
        }

        i = j;
    }

    // Aggregate stats
    uint64_t sum = 0;
    for (auto d : depth) {
        sum += d;
        if (d > res.max_depth) res.max_depth = d;
        if (d > 0) ++res.count;
    }

    const double n = (double)depth.size();
    res.mean_all_elements = (n > 0) ? (double)sum / n : 0.0;
    res.mean_overtaken_elements = (res.count > 0) ? (double)sum / (double)res.count : 0.0;
    res.pct = (n > 0) ? 100.0 * (double)res.count / n : 0.0;

    return res;
}



