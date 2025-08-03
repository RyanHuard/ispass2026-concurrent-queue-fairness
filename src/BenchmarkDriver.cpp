#include <thread>
#include <vector>
#include <iostream>

#include "msqueue.hpp"
#include "../include/FairnessLogger.hpp"
#include "../include/clocks/HighResolutionClock.hpp"


int main(int argc, char *argv[]) {
  using namespace std::chrono;
  const int num_ops = 10'000;
  const int trials = 100;

  for (int num_threads = 1; num_threads <= 16; ++num_threads) {
    long long total_time = 0;
    double total_tfi = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
      FairnessLogger logger;
      HighResolutionClock clock;
      MSQueue<int> q(logger, clock);
      int num_ops_per_thread = num_ops / num_threads;

      auto start = high_resolution_clock::now();

      auto worker = [&](int tid) {
        int value;
        for (int i = 0; i < 200; i++) q.enqueue(i, tid);
        for (int i = 0; i < num_ops_per_thread; ++i) {
          q.enqueue(i, tid);
          while (!q.dequeue(&value, tid)) std::this_thread::yield();
        }
        for (int i = 0; i < 200; i++) q.dequeue(&value, tid);
      };

      std::vector<std::thread> threads;
      for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker, t);
      }
      for (auto& thread : threads) thread.join();

      auto end = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(end - start).count();
      total_time += duration;
      total_tfi += compute_thread_fairness(q.records);
    }

    std::cout << "Threads: " << num_threads
              << "  Avg Time (ms): " << (total_time / trials)
              << "  Avg Thread-fairness Index: " << (total_tfi / trials)
              << "\n";
  }
}