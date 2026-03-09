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
#include <type_traits>
#include <atomic>
#include <chrono>
#include <cstring>
#include <numeric>

#include <papi.h>
#include "CacheMisses.hpp"

#include "msqueue.hpp"
#include "fcqueue.hpp"
#include "LCRQueue.hpp"
#include "LPRQueue.hpp"
#include "FAAArrayQueue.hpp"
// #include "SCQueue.hpp"
#include "args.hpp"
#include "Workloads.hpp"
#include "FairnessLogger.hpp"

using bench::Options;
using bench::parse_args;

template <typename T, typename = void>
struct has_commit_logs : std::false_type
{
};

template <typename T>
struct has_commit_logs<T, std::void_t<decltype(std::declval<T>().commit_thread_logs())>>
    : std::true_type
{
};

template <typename Q>
void try_commit_logs(Q &q)
{
    if constexpr (has_commit_logs<Q>::value)
    {
        q.commit_thread_logs();
    }
}

template <typename T, typename = void>
struct has_records : std::false_type
{
};

template <typename T>
struct has_records<T, std::void_t<decltype(std::declval<T>().records)>>
    : std::true_type
{
};



template <typename Q>
Q make_queue(int /*num_threads*/) { return Q{}; }

template <>
FlatCombiningQueue<int> make_queue<FlatCombiningQueue<int>>(int num_threads)
{
    return FlatCombiningQueue<int>(num_threads);
}

template <>
FAAArrayQueueAdapter<int, false, 1024, true>
make_queue<FAAArrayQueueAdapter<int, false, 1024, true>>(int num_threads)
{
    return FAAArrayQueueAdapter<int, false, 1024, true>(num_threads);
}

// ---------------------------------------------------------

template <typename Q>
void run_benchmark(const Options &opts,
                   const std::string &qname,
                   const std::string &workload_name)
{
    using namespace std::chrono;

    const int num_ops = opts.ops;
    const int trials = opts.trials;
    const int max_threads = opts.max_threads;

    std::string filename = qname + "_" + workload_name + ".csv";
    std::ofstream csv("results/" + filename);

    if (!csv.is_open())
    {
        std::cerr << "Failed to open output file: " << filename << "\n";
        return;
    }

    fprintf(stderr, "Output=%s\n", filename.c_str());

    csv << "threads,trial,ms,"
           "enq_mean_all,enq_mean_ovt,enq_max,enq_count,enq_pct,"
           "deq_mean_all,deq_mean_ovt,deq_max,deq_count,deq_pct,"
           "l1_missrate\n";

    for (int num_threads = 1; num_threads <= max_threads; ++num_threads)
    {
        for (int trial = 0; trial < trials; ++trial)
        {

            // Store the calculated miss rates
            std::vector<double> thread_miss_rates(num_threads, 0);

            Q q = make_queue<Q>(num_threads);
            std::barrier sync(num_threads);

            auto worker_thread = [&](int tid)
            {
                // Initialize PAPI for this thread
                if (PAPI_thread_init(pthread_self) != PAPI_OK)
                {
                }

                CacheMissCounters l1;

                Workload workload;
                if (workload_name == "enqueueheavy")
                    workload = Workload::EnqueueHeavy;
                else if (workload_name == "dequeueheavy")
                    workload = Workload::DequeueHeavy;
                else if (workload_name == "pair")
                    workload = Workload::EnqueueDequeuePair;
                else
                {
                    std::cerr << "Unknown workload: " << workload_name << "\n";
                    workload = Workload::EnqueueDequeuePair;
                }

                // Pre-fill for pair workload
                if (workload_name == "pair" && tid == 0)
                {
                    for (int i = 0; i < 5000; i++)
                        q.enqueue(i, tid);
                }

                sync.arrive_and_wait();

                l1.start_l1_cache_miss_counting();
                worker(q, tid, num_ops, workload, sync, num_threads);
                l1.stop_l1_cache_miss_counting();

                try_commit_logs(q);

                // Calculate local accesses based on workload
                long long local_accesses = num_ops;
                if (workload_name == "pair")
                    local_accesses *= 2;

                thread_miss_rates[tid] = l1.get_l1_cache_miss_rate(local_accesses);
            };

            auto start = high_resolution_clock::now();

            std::vector<std::thread> threads;
            threads.reserve(num_threads);
            for (int t = 0; t < num_threads; t++)
                threads.emplace_back(worker_thread, t);

            for (auto &thread : threads)
                thread.join();

            auto end = high_resolution_clock::now();
            long long duration_ms =
                duration_cast<milliseconds>(end - start).count();

            double enq_mean = 0, enq_ovt = 0, enq_max = 0, enq_cnt = 0, enq_pct = 0;
            double deq_mean = 0, deq_ovt = 0, deq_max = 0, deq_cnt = 0, deq_pct = 0;
            size_t records_size = 0;

            if constexpr (has_records<Q>::value)
            {
                auto &records = q.records;
                records_size = records.size();

                if (!records.empty())
                {
                    size_t cutoff = std::min<size_t>(200 * num_threads, records_size);
                    records.erase(records.begin(), records.begin() + cutoff);
                }

                if (!records.empty())
                {
                    auto enq = enqueue_fairness(records);
                    auto deq = dequeue_fairness(records);

                    enq_mean = enq.mean_all_elements;
                    enq_ovt = enq.mean_overtaken_elements;
                    enq_max = enq.max_depth;
                    enq_cnt = enq.count;
                    enq_pct = enq.pct;

                    deq_mean = deq.mean_all_elements;
                    deq_ovt = deq.mean_overtaken_elements;
                    deq_max = deq.max_depth;
                    deq_cnt = deq.count;
                    deq_pct = deq.pct;
                }
            }

            fprintf(stderr,
                    "Threads=%d Trial=%d done.\n",
                    num_threads, trial);

            double avg_miss_rate = 0.0;
            if (num_threads > 0)
            {
                double total_rate = std::accumulate(thread_miss_rates.begin(), thread_miss_rates.end(), 0.0);
                avg_miss_rate = total_rate / num_threads;
            }

            // CSV write
            csv << std::fixed << std::setprecision(6)
                << num_threads << ","
                << trial << ","
                << duration_ms << ","
                << enq_mean << ","
                << enq_ovt << ","
                << enq_max << ","
                << enq_cnt << ","
                << enq_pct << ","
                << deq_mean << ","
                << deq_ovt << ","
                << deq_max << ","
                << deq_cnt << ","
                << deq_pct << ","
                << avg_miss_rate
                << "\n";
        }
    }

    csv.close();
}

int main(int argc, char *argv[])
{
    if (PAPI_library_init(PAPI_VER_CURRENT) != PAPI_VER_CURRENT)
    {
        std::cerr << "FATAL: PAPI library initialization failed.\n";
        exit(1);
    }

    if (PAPI_thread_init(pthread_self) != PAPI_OK)
    {
        std::cerr << "FATAL: PAPI thread init failed.\n";
        exit(1);
    }

    Options opts = parse_args(argc, argv);

    const std::vector<std::string> workloads =
        {"enqueueheavy", "pair", "dequeueheavy"};

    for (const auto &qname : opts.queues)
    {
        for (const auto &workload : workloads)
        {
            if (qname == "ms")
            {
                run_benchmark<MSQueue<int>>(opts, "ms", workload);
            }
            else if (qname == "fc")
            {
                run_benchmark<FlatCombiningQueue<int>>(opts, "fc", workload);
            }
            else if (qname == "lprq")
            {
                run_benchmark<PRQueueAdapter<int, false, 1024, true>>(opts, "lprq", workload);
            }
            else if (qname == "faa")
            {
                run_benchmark<FAAArrayQueueAdapter<int, false, 1024, true>>(opts, "faa", workload);
            }
            else
            {
                std::cerr << "Unknown queue type: " << qname << "\n";
            }

            fprintf(stderr,
                    "Workload=%s on %s complete.\n",
                    workload.c_str(), qname.c_str());
        }
    }

    return 0;
}