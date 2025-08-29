#include <thread>
#include <vector>
#include <iostream>
#include <pthread.h>
#include <sched.h>

#include "msqueue.hpp"
#include "../include/FairnessLogger.hpp"
#include "../include/clocks/HighResolutionClock.hpp"

std::atomic<int> ready_count{0};

int main(int argc, char *argv[]) {
  using namespace std::chrono;
  const int num_ops = 10'000;
  const int trials = 10;

  for (int num_threads = 1; num_threads <= 32; ++num_threads) {
    long long total_time = 0;
    double total_enq_to_deq_overtake_pct = 0.0;
    double total_enqueue_overtake_depth_mean = 0.0;
    size_t total_enqueue_overtake_depth_max = 0;

    for (int trial = 0; trial < trials; ++trial) {
      MSQueue<int> q;
      int num_ops_per_thread = num_ops / num_threads;

      // for (int i = 0; i < 200; i++) q.enqueue(i);
      int value;

      auto start = high_resolution_clock::now();

      auto worker = [&]() {
        for (int i = 0; i < num_ops_per_thread; ++i) {
          q.enqueue(i);
          while (!q.dequeue(&value)) std::this_thread::yield();
        }
		    
      };

      // for (int i = 0; i < 200; i++) q.dequeue(&value);

      std::vector<std::thread> threads;
      for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
      }
      for (auto& thread : threads) thread.join();

      auto end = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(end - start).count();
      total_time += duration;
      
     // OvertakeDepthStats enqueue_overtake_depth_stats = compute_enqueue_overtake_depth(q.records); // this function is about call_ts vs in_ts

     total_enq_to_deq_overtake_pct += compute_enq_to_deq_overtake_percentage(q.records); // this function is about call_ts vs deq_ts
     // total_enqueue_overtake_depth_mean += enqueue_overtake_depth_stats.mean_overtaken;
    //  total_enqueue_overtake_depth_max += enqueue_overtake_depth_stats.max_depth;
    }

    std::cout << "Threads: " << num_threads
              << "  Avg Time (ms): " << (total_time / trials)
              << "  Overtaken % (From enq -> deq): " << (total_enq_to_deq_overtake_pct / trials)
            //  << "  Avg Overtaken Depth: " << (total_enqueue_overtake_depth_mean / trials) /
            //  << "  Max Overtaken Depth: " << (total_enqueue_overtake_depth_max / trials)
              << "\n";
  }
}
