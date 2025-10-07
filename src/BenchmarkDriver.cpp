#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <pthread.h>
#include <sched.h>
#include <unordered_map>
#include <tuple>
#include <ostream>
#include <barrier>

#include "msqueue.hpp"
#include "fcqueue.hpp"
#include "scqueue.hpp"
#include "lcrqueue.hpp"
#include "args.hpp"
#include "Workloads.hpp"
#include "FairnessLogger.hpp"



using bench::Options;
using bench::parse_args;


int main(int argc, char *argv[]) {
  Options opts = parse_args(argc, argv);

  using namespace std::chrono;
  const int num_ops = opts.ops; // each thread does this many ops      default = 10,000
  const int trials = opts.trials; // trials per thread count           default = 10
  const int max_threads = opts.max_threads; //                         default = 32

  // ---- CSV header ----
  std::cout << "threads,avg_ms,"
               "ins_mean_all,ins_mean_ovt,ins_max,ins_count_avg,ins_pct_avg\n";

  for (int num_threads = 1; num_threads <= max_threads; ++num_threads) {
    // accumulators for measurements
    long long total_time = 0;
    double total_insertion_mean_all_elements = 0.0, total_insertion_mean_overtaken_elements = 0.0, total_insertion_pct = 0.0;
    size_t total_insertion_count = 0;
    size_t max_insertion_depth = 0;


    // common practice is to ignore the first trial or two because of hardware warmup
    for (int trial = 0; trial < trials + 2; trial++) {
     //MSQueue<int> q;
     //FlatCombiningQueue<int> q(num_threads);
     //SySQueue q(num_threads);
     LCRQ<int> q;
  

      auto start = high_resolution_clock::now();

      std::barrier sync(num_threads);
      auto worker_thread = [&](int tid) {
        Workload workload;
        workload = Workload::DequeueHeavy;

        worker(q, tid, num_ops, workload, sync);
      };

      std::vector<std::thread> threads;
      for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker_thread, t);
      }
      for (auto& thread : threads) thread.join();

      auto end = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(end - start).count();
      

      // ignore the first 2 trials
      if (trial < 2) continue;

      total_time += duration;

    // size_t in_ts_zero = 0, deq_before_in = 0;
    // for (auto& rec : q.records) {
    //     uint64_t call_ts = std::get<0>(rec);
    //     uint64_t in_ts   = std::get<1>(rec);
    //     uint64_t deq_ts  = std::get<2>(rec);
    //     if (in_ts == 0) ++in_ts_zero;
    //     if (deq_ts < in_ts) ++deq_before_in;
    // }
    // std::cerr << "in_ts_zero=" << in_ts_zero
    //           << " deq_before_in=" << deq_before_in
    //           << " records=" << q.records.size() << "\n";

          
      
      
       // Fairness stats for this trial
      auto& records = q.records;  
      
      // removes the prefill elements
      records.erase(records.begin(), records.begin() + std::min<size_t>(200 * num_threads, records.size())); 


      auto ins = insertion_fairness(records);
      
      total_insertion_mean_all_elements += ins.mean_all_elements;
      total_insertion_mean_overtaken_elements += ins.mean_overtaken_elements;
      total_insertion_pct  += ins.pct;
      total_insertion_count += ins.count;
      max_insertion_depth = std::max(max_insertion_depth, ins.max_depth);
    
     }

    // averages across trials
    double avg_ms              = static_cast<double>(total_time) / trials;

    double ins_mean_all_elements_avg        = total_insertion_mean_all_elements / trials;
    double ins_mean_overtaken_elements_avg = total_insertion_mean_overtaken_elements / trials;
    double ins_pct_avg         = total_insertion_pct / trials;
    double ins_count_avg       = static_cast<double>(total_insertion_count) / trials;

    // This should go to a CSV
    std::cout << std::fixed << std::setprecision(3)
              << num_threads << ','
              << avg_ms << ','
              << ins_mean_all_elements_avg << ',' << ins_mean_overtaken_elements_avg << ',' << max_insertion_depth << ',' << ins_count_avg << ',' << ins_pct_avg << '\n';

    fprintf(stderr, "Threads=%d complete.\n", num_threads);
  }
}
