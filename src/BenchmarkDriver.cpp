#include <thread>
#include <vector>
#include <iostream>

#include "msqueue.hpp"

int main(int argc, char *argv[]) {
  const int num_threads = 8;
  const int num_ops = 1'000'000;

  
  
  for (int num_threads = 1; num_threads <= 16; ++num_threads) {
    MSQueue<int> q;
    int num_ops_per_thread = num_ops / num_threads;
    
    auto start = std::chrono::high_resolution_clock::now();

    auto worker = [&](int tid) {
        for (int i = 0; i < num_ops_per_thread; ++i) {
            q.enqueue(i);
            int value;
            q.dequeue(&value);
        }
    };

  std::vector<std::thread> threads;
  for (int t = 0; t < num_threads; t++) {
    threads.emplace_back(worker, t);
  }
  for (auto& thread : threads) thread.join();
     auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "Threads: " << num_threads
              << "  Time (ms): " << duration << std::endl;
  }
}
