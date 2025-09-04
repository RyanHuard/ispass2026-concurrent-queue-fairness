#include <thread>
#include <vector>
#include <iostream>
#include <iomanip>
#include <pthread.h>
#include <sched.h>
#include <unordered_map>
#include <tuple>
#include <ostream>

#include "msqueue.hpp"
#include "fcqueue.hpp"
#include "scqueue.hpp"

#include "../include/FairnessLogger.hpp"
#include "../include/clocks/HighResolutionClock.hpp"

std::atomic<int> ready_count{0};

int main(int argc, char *argv[]) {
  using namespace std::chrono;
  const int num_ops = 10'000; // each thread does this many
  const int trials = 10;

  // ---- CSV header ----
  std::cout << "threads,avg_ms,"
               "ins_mean_all,ins_mean_ovt,ins_max,ins_count_avg,ins_pct_avg,"
               "deq_mean_all,deq_mean_ovt,deq_max,deq_count_avg,deq_pct_avg,"
               "e2e_mean_all,e2e_mean_ovt,e2e_max,e2e_count_avg,e2e_pct_avg\n";

  for (int num_threads = 1; num_threads <= 16; ++num_threads) {
    // accumulators for measurements
    long long total_time = 0;
    double total_insertion_mean_all_elements = 0.0, total_insertion_mean_overtaken_elements = 0.0, total_insertion_pct = 0.0;
    size_t total_insertion_count = 0;
    size_t max_insertion_depth = 0;

    double total_dequeue_mean_all_elements = 0.0, total_dequeue_mean_overtaken_elements = 0.0, total_dequeue_pct = 0.0;
    size_t total_dequeue_count = 0;
    size_t max_dequeue_depth = 0;

    double total_end2end_mean_all_elements = 0.0, total_end2end_mean_overtaken_elements = 0.0, total_end2end_pct = 0.0;
    size_t total_end2end_count = 0;
    size_t max_end2end_depth = 0;

    for (int trial = 0; trial < trials; ++trial) {
     MSQueue<int> q;
    // FlatCombiningQueue<int> q(num_threads);
    //SySQueue<int> q(num_threads);

     int num_ops_per_thread = num_ops / num_threads;

      auto start = high_resolution_clock::now();

      auto worker = [&](int tid) {
        int value;
       // for (int i = 0; i < 200; i++) q.enqueue(i, tid);

        for (int i = 0; i < num_ops; ++i) {
          q.enqueue(i, tid);
          while (!q.dequeue(&value, tid)) std::this_thread::yield();
        }
		    //for (int i = 0; i < 200; i++) q.dequeue(&value, tid);
      };

      std::vector<std::thread> threads;
      for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker, t);
      }
      for (auto& thread : threads) thread.join();

      auto end = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(end - start).count();
      total_time += duration;


       // ---- Fairness stats for this trial ----
      const auto& records = q.records;

      auto ins = insertion_fairness(records);
      auto deq = service_fairness(records);
      auto e2e = end_to_end_fairness(records);


      total_insertion_mean_all_elements += ins.mean_all_elements;
      total_insertion_mean_overtaken_elements += ins.mean_overtaken_elements;
      total_insertion_pct  += ins.pct;
      total_insertion_count += ins.count;
      max_insertion_depth = std::max(max_insertion_depth, ins.max_depth);

      total_dequeue_mean_all_elements += deq.mean_all_elements;
      total_dequeue_mean_overtaken_elements += e2e.mean_overtaken_elements;
      total_dequeue_pct  += deq.pct;
      total_dequeue_count += deq.count;
      max_dequeue_depth = std::max(max_dequeue_depth, deq.max_depth);

      total_end2end_mean_all_elements += e2e.mean_all_elements;
      total_end2end_mean_overtaken_elements += e2e.mean_overtaken_elements;
      total_end2end_pct  += e2e.pct;
      total_end2end_count += e2e.count;
      max_end2end_depth = std::max(max_end2end_depth, e2e.max_depth);
     }

    // averages across trials
    double avg_ms              = static_cast<double>(total_time) / trials;

    double ins_mean_all_elements_avg        = total_insertion_mean_all_elements / trials;
    double ins_mean_overtaken_elements_avg = total_insertion_mean_overtaken_elements / trials;
    double ins_pct_avg         = total_insertion_pct / trials;
    double ins_count_avg       = static_cast<double>(total_insertion_count) / trials;

    double deq_mean_all_elements_avg        = total_dequeue_mean_all_elements / trials;
    double deq_mean_overtaken_elements_avg = total_dequeue_mean_overtaken_elements / trials;
    double deq_pct_avg         = total_dequeue_pct / trials;
    double deq_count_avg       = static_cast<double>(total_dequeue_count) / trials;

    double e2e_mean_all_elements_avg        = total_end2end_mean_all_elements / trials;
    double e2e_mean_overtaken_elements_avg = total_end2end_mean_overtaken_elements / trials;
    double e2e_pct_avg         = total_end2end_pct / trials;
    double e2e_count_avg       = static_cast<double>(total_end2end_count) / trials;

    // ---- CSV row ----
    std::cout << std::fixed << std::setprecision(3)
              << num_threads << ','
              << avg_ms << ','
              << ins_mean_all_elements_avg << ',' << ins_mean_overtaken_elements_avg << ',' << max_insertion_depth << ',' << ins_count_avg << ',' << ins_pct_avg << ','
              << deq_mean_all_elements_avg << ',' << deq_mean_overtaken_elements_avg << ',' << max_dequeue_depth   << ',' << deq_count_avg << ',' << deq_pct_avg << ','
              << e2e_mean_all_elements_avg << ',' << e2e_mean_overtaken_elements_avg << ',' << max_end2end_depth << ',' << e2e_count_avg << ',' << e2e_pct_avg
              << '\n';

    //  std::cout << "Threads: " << num_threads
    //           << "  Avg Time (ms): " << (total_time / trials)
    //           << "\n  Insertion Fairness: mean=" << (total_insertion_mean / trials)
    //           << ", max=" << max_insertion_depth
    //           << ", count=" << (total_insertion_count / trials)
    //           << ", pct=" << (total_insertion_pct / trials) << "%"
    //           << "\n  dequeue Fairness:   mean=" << (total_dequeue_mean / trials)
    //           << ", max=" << max_dequeue_depth
    //           << ", count=" << (total_dequeue_count / trials)
    //           << ", pct=" << (total_dequeue_pct / trials) << "%"
    //           << "\n  End-to-End Fairness:mean=" << (total_end2end_mean / trials)
    //           << ", max=" << max_end2end_depth
    //           << ", count=" << (total_end2end_count / trials)
    //           << ", pct=" << (total_end2end_pct / trials) << "%"
    //           << "\n\n";

    fprintf(stderr, "Threads=%d complete.\n", num_threads);
  }
}
