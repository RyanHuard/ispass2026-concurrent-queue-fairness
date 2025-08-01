#include <thread>
#include <vector>
#include <iostream>

#include "msqueue.hpp"
#include "../include/FairnessLogger.hpp"
#include "../include/clocks/HighResolutionClock.hpp"


int main(int argc, char *argv[]) {
  using namespace std::chrono;
  const int num_threads = 1;
  const int num_ops = 10'0000;


  for (int num_threads = 1; num_threads <= 16; ++num_threads) {
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
          while(!q.dequeue(&value)) std::this_thread::yield();
        }
	for (int i = 0; i < 200; i++) q.dequeue(&value);
    };

  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; t++) {
    threads.emplace_back(worker, t);
  }
  for (auto& thread : threads) thread.join();

  auto end = high_resolution_clock::now();
   auto duration = duration_cast<milliseconds>(end - start).count();
   
    
     std::cout << "Threads: " << num_threads
	       << "  Time (ms): " << duration
        << "\nThread-fairness Index: " << compute_thread_fairness(q.records) << "\n"
	       <<"Overtake %: " << compute_overtake_percentage(q.records) << "\n";
  }

 
  
}
