#pragma once
#include <thread>
#include <algorithm>
#include <random>
#include <vector>
#include <iostream>
#include <cmath>
#include <barrier>
#include <pthread.h>
#include <sched.h>

enum class Workload
{
    EnqueueHeavy,
    DequeueHeavy,
    EnqueueDequeuePair
};

// Enqueue = 0, Dequeue = 1
inline std::vector<int> make_ops_with_ratio(int total, double enq_frac, int seed = 0)
{
    int e = std::max(0, std::min(total, int(std::round(total * enq_frac))));
    int d = total - e;

    std::vector<int> ops;
    ops.reserve(total);
    ops.insert(ops.end(), e, 0);
    ops.insert(ops.end(), d, 1);

    std::default_random_engine eng(seed ? seed : std::random_device{}());
    std::shuffle(ops.begin(), ops.end(), eng);
    return ops;
}

template <typename Queue>
void worker(Queue &q, int tid, int num_ops, Workload workload, std::barrier<> &sync, int num_threads)
{
    int cores = std::thread::hardware_concurrency();
    if (cores == 0)
        cores = 1;
    int core = tid % cores;

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t),
                           &cpuset);

    double ratio = 0.0;
    switch (workload)
    {
    case Workload::EnqueueHeavy:
        ratio = 0.67;
        break;
    case Workload::DequeueHeavy:
        ratio = 0.33;
        break;
    case Workload::EnqueueDequeuePair:
        ratio = 1.00;
        break;
    }

    int total_ops = num_threads * num_ops;
    int total_enq = int(std::round(total_ops * ratio));
    int total_deq = total_ops - total_enq;

    int per_thread_enq = total_enq / num_threads;
    int per_thread_deq = total_deq / num_threads;

    int prefill = 0;

    if (workload == Workload::EnqueueHeavy)
    {
        prefill = 0; // start empty
    }
    else if (workload == Workload::DequeueHeavy)
    {
        // Prefill must cover global dequeue deficit
        int deficit = total_deq - total_enq;
        prefill = std::max(0, deficit) + 1000;
    }
    else if (workload == Workload::EnqueueDequeuePair)
    {
        prefill = 1000;
    }

    if (tid == 0 && prefill > 0)
    {
        for (int i = 0; i < prefill; i++)
        {
            q.enqueue(i, 0); // treat as setup
        }
    }

    // Ensure prefilled state visible
    sync.arrive_and_wait();

    std::vector<int> ops;
    ops.reserve(num_ops);

    if (workload != Workload::EnqueueDequeuePair)
    {
        ops.insert(ops.end(), per_thread_enq, 0);
        ops.insert(ops.end(), per_thread_deq, 1);

        // Shuffle operation ordering
        std::shuffle(
            ops.begin(),
            ops.end(),
            std::default_random_engine(tid));
    }

    int value = 0;
    sync.arrive_and_wait();

    if (workload == Workload::EnqueueDequeuePair)
    {
        for (int i = 0; i < num_ops; i++)
        {
            q.enqueue(i, tid);
            q.dequeue(&value, tid);
        }
    }
    else
    {
        for (int op : ops)
        {
            if (op == 0)
                q.enqueue(1, tid);
            else
                q.dequeue(&value, tid);
        }
    }

    //
    sync.arrive_and_wait();
}
