#pragma once
#include <thread>
#include <algorithm>
#include <random>
#include <vector>
#include <iostream>
#include <cmath>
#include <barrier>



enum class Workload { 
    EnqueueHeavy, 
    Balanced, 
    DequeueHeavy,
    EnqueueDequeuePair
};

// Enqueue = 0, Dequeue = 1
inline std::vector<int> make_ops_with_ratio(int total, double enq_frac, int seed = 0) {
    int e = std::max(0, std::min(total, int(std::round(total * enq_frac))));
    int d = total - e;

    std::vector<int> ops; 
    ops.reserve(total);
    ops.insert(ops.end(), e, 0);
    ops.insert(ops.end(), d, 1);

    // Simple engine, reproducible with seed (e.g. thread id)
    std::default_random_engine eng(seed ? seed : std::random_device{}());
    std::shuffle(ops.begin(), ops.end(), eng);
    return ops;
}


template<typename Queue>
void worker(Queue& q, int tid, int num_ops, Workload workload, std::barrier<>& sync) {
    const int num_cores = std::thread::hardware_concurrency();
    int core = tid % num_cores;
    
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) std::cerr << "pthread";

    int prefill = 200;
    double ratio; // used for queue draining (ratio of enq:num_ops)
    int value;
    for (int i = 0; i < prefill; i++) {
        q.enqueue(i, tid);
    }

    switch (workload) {     
        case Workload::EnqueueDequeuePair:
            ratio = 1;
            for (int i = 0; i < num_ops; i++) {
                q.enqueue(i, tid);
                while (!q.dequeue(&value, tid)) std::this_thread::yield();
            }
            break;
        
        case Workload::Balanced: {
            ratio = 0.5;
            auto ops = make_ops_with_ratio(num_ops, ratio, tid);
            int e = 0, d = 0;
            for (int i = 0; i < (int)ops.size(); ++i) {
                if (ops[i] == 0) { 
                    q.enqueue(10, tid); 
                    e++; 
                }
                else { 
                    while (!q.dequeue(&value, tid)) {
                        std::this_thread::yield(); 
                    }
                    d++;
                }
            }
            break;
        }

        case Workload::EnqueueHeavy: {
            ratio = 0.67;
            auto ops = make_ops_with_ratio(num_ops, ratio, tid);
            int e = 0, d = 0;
            for (int i = 0; i < (int)ops.size(); ++i) {
                if (ops[i] == 0) { 
                    q.enqueue(10, tid); 
                    e++; 
                }
                else { 
                    while (!q.dequeue(&value, tid)) {
                        std::this_thread::yield(); 
                    }
                    d++;
                }
            }
            break;
        }

        case Workload::DequeueHeavy: {
            ratio = 0.33;
            auto ops = make_ops_with_ratio(num_ops, ratio, tid);
            int e = 0, d = 0;
            for (int i = 0; i < (int)ops.size(); ++i) {
                if (ops[i] == 0) { 
                    q.enqueue(i, tid); 
                    e++; }
                else { 
                   q.dequeue(&value, tid);
                 
                    
                    d++;
                }
            }
            break;
        }
    }

    sync.arrive_and_wait();

    for (int i = 0; i < ratio * num_ops + prefill; i++) {
        q.dequeue(&value, tid);
    }
}