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
    double total_tfi = 0.0;
    double total_overtake = 0.0;

    for (int trial = 0; trial < trials; ++trial) {
      MSQueue<int> q;
      int num_ops_per_thread = num_ops / num_threads;

      auto start = high_resolution_clock::now();

      // TODO: I commented out the 200 enq prefill the start time was including those values
      auto worker = [&]() {
        int value;
	      //for (int i = 0; i < 200; i++) q.enqueue(i);
        for (int i = 0; i < num_ops_per_thread; ++i) {
          q.enqueue(i);
          while (!q.dequeue(&value)) std::this_thread::yield();
        }
		    //for (int i = 0; i < 200; i++) q.dequeue(&value);
      };

      std::vector<std::thread> threads;
      for (int t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
      }
      for (auto& thread : threads) thread.join();

      auto end = high_resolution_clock::now();
      auto duration = duration_cast<milliseconds>(end - start).count();
      total_time += duration;
      total_overtake += compute_overtake_percentage(q.records);
    }

    std::cout << "Threads: " << num_threads
              << "  Avg Time (ms): " << (total_time / trials)
              << "  Overtaken %: " << (total_overtake / trials)
              << "\n";
  }
}
