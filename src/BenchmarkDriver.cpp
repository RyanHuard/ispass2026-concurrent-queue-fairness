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
#include <fstream>

#include "msqueue.hpp"
#include "fcqueue.hpp"
#include "lcrqueue.hpp"
#include "args.hpp"
#include "Workloads.hpp"
#include "FairnessLogger.hpp"

using bench::Options;
using bench::parse_args;

// make_queue is needed to deal with the FC queue requiring num_threads in constructor
template <typename Q>
Q make_queue(int num_threads) {
  return Q{};
}

// Specialization for FlatCombiningQueue
template <>
FlatCombiningQueue<int> make_queue<FlatCombiningQueue<int>>(int num_threads) {
  return FlatCombiningQueue<int>(num_threads);
}


template <typename Q>
void run_benchmark(const Options& opts, const std::string& qname, const std::string& workload_name) {
  using namespace std::chrono;
  const int num_ops = opts.ops;        // each thread does this many ops      default = 10,000
  const int trials = opts.trials;      // trials per thread count             default = 10
  const int max_threads = opts.max_threads; //                                default = 32

  std::string filename = qname + "_" + workload_name + ".csv";
  
  std::ofstream csv(filename);
  if (!csv.is_open()) {
    std::cerr << "Failed to open output file: " << filename << "\n";
    return;
  }

  csv << "threads,avg_ms,"
         "enq_mean_all,enq_mean_ovt,enq_max,enq_count_avg,enq_pct_avg,"
         "deq_mean_all,deq_mean_ovt,deq_max,deq_count_avg,deq_pct_avg\n";
  fprintf(stderr, "test1");
  for (int num_threads = 1; num_threads <= max_threads; ++num_threads) {
    // accumulators for measurements
    long long total_time = 0;
    OvertakeMetrics total_enqueue_metrics; 
    OvertakeMetrics total_dequeue_metrics;
    

    // common practice is to ignore the first trial or two because of hardware warmup
    for (int trial = 0; trial < trials + 2; trial++) {
      Q q = make_queue<Q>(num_threads);

      auto start = high_resolution_clock::now();

      std::barrier sync(num_threads);
      auto worker_thread = [&](int tid) {
        Workload workload;

        if (workload_name == "enqueueheavy")      workload = Workload::EnqueueHeavy;
        else if (workload_name == "dequeueheavy") workload = Workload::DequeueHeavy;
        else if (workload_name == "balanced")     workload = Workload::Balanced;
        else if (workload_name == "pair")         workload = Workload::EnqueueDequeuePair;
        else {
          std::cerr << "Unknown workload: " << workload_name << "\n";
          workload = Workload::EnqueueDequeuePair;
        }

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

      // Fairness stats for this trial ONLY
      auto& records = q.records;

      // removes the prefill elements
      records.erase(records.begin(), records.begin() + std::min<size_t>(200 * num_threads, records.size()));

      OvertakeMetrics enqueue = enqueue_fairness(records);
      OvertakeMetrics dequeue = dequeue_fairness(records);

      // accumulate stats across trials
      total_enqueue_metrics.mean_all_elements       += enqueue.mean_all_elements;
      total_enqueue_metrics.mean_overtaken_elements += enqueue.mean_overtaken_elements;
      total_enqueue_metrics.max_depth                = std::max(total_enqueue_metrics.max_depth, enqueue.max_depth);
      total_enqueue_metrics.count                   += enqueue.count;
      total_enqueue_metrics.pct                     += enqueue.pct;

      total_dequeue_metrics.mean_all_elements       += dequeue.mean_all_elements;
      total_dequeue_metrics.mean_overtaken_elements += dequeue.mean_overtaken_elements;
      total_dequeue_metrics.max_depth                = std::max(total_dequeue_metrics.max_depth, dequeue.max_depth);
      total_dequeue_metrics.count                   += dequeue.count;
      total_dequeue_metrics.pct                     += dequeue.pct;
    }

     // averages across trials
    double avg_ms = static_cast<double>(total_time) / trials;

    total_enqueue_metrics.mean_all_elements       /= trials;
    total_enqueue_metrics.mean_overtaken_elements /= trials;
    total_enqueue_metrics.pct                     /= trials;
    double avg_enq_count = static_cast<double>(total_enqueue_metrics.count) / trials;

    total_dequeue_metrics.mean_all_elements       /= trials;
    total_dequeue_metrics.mean_overtaken_elements /= trials;
    total_dequeue_metrics.pct                     /= trials;
    double avg_deq_count = static_cast<double>(total_dequeue_metrics.count) / trials;

    // This should go to a CSV
    csv << std::fixed << std::setprecision(3)
              << num_threads << ','
              << avg_ms << ','
              << total_enqueue_metrics.mean_all_elements << ','
              << total_enqueue_metrics.mean_overtaken_elements << ','
              << total_enqueue_metrics.max_depth << ','
              << avg_enq_count << ','
              << total_enqueue_metrics.pct << ','
              << total_dequeue_metrics.mean_all_elements << ','
              << total_dequeue_metrics.mean_overtaken_elements << ','
              << total_dequeue_metrics.max_depth << ','
              << avg_deq_count << ','
              << total_dequeue_metrics.pct << '\n';

    
    fprintf(stderr, "Threads=%d complete.\n", num_threads);
  }
  csv.close();
}


int main(int argc, char *argv[]) {
  Options opts = parse_args(argc, argv);

  const std::vector<std::string> workloads = {"enqueueheavy", "balanced", "pair"};

  for (const auto& qname : opts.queues) {
    for (const auto& workload : workloads) {
      if (qname == "ms") {
        run_benchmark<MSQueue<int>>(opts, "ms", workload);
      } 
      else if (qname == "fc") {
        run_benchmark<FlatCombiningQueue<int>>(opts, "fc", workload);
      } 
      else if (qname == "lcrq") {
        run_benchmark<LCRQ<int>>(opts, "lcrq", workload);
      } 
      else {
        std::cerr << "Unknown queue type: " << qname << "\n";
      }
    }
  }
}
