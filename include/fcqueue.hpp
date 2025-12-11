#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <tuple>
#include "FairnessLogger.hpp"

using namespace std;

// -----------------------------------------------------------
// 1. Thread Local Storage Helper
// -----------------------------------------------------------
using FCLogRecord = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;

struct FCThreadLogStorage {
    std::vector<FCLogRecord> events;
    FCThreadLogStorage() {
        events.reserve(2000000); 
    }
};

static thread_local FCThreadLogStorage fc_tls_log;

// -----------------------------------------------------------
// 2. Data Structures
// -----------------------------------------------------------

enum class Opcode : uint8_t { Done = 0, Enqueue = 1, Dequeue = 2 };

template <typename T>
struct FCNode {
    T        value{};

    // These travel with the node through the queue
    uint64_t enq_inv_ts; 
    uint64_t enq_lin_ts; 
    
    // These are used to pass data back from Combiner -> Dequeuer
    uint64_t deq_inv_ts; 
    uint64_t deq_lin_ts; 
};

template <typename T>
struct alignas(64) Operation {
    std::atomic<Opcode> opcode{Opcode::Done};
    FCNode<T>           node{};  
    bool                success = false; 
};

template <typename T>
class FlatCombiningQueue {
public:
    // Global records for final merge
    std::vector<FCLogRecord> records;
    std::mutex records_mutex;

    std::atomic<int> enq_retries{0};

    explicit FlatCombiningQueue(size_t num_threads)
        : operations_(num_threads) {}

    bool enqueue(const T& item, int tid) {
        uint64_t enq_inv_ts = now();

        auto& slot = operations_[tid];
        slot.node.enq_inv_ts = enq_inv_ts;
        slot.node.value   = item;
        slot.opcode.store(Opcode::Enqueue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { 
                scan_combine_apply(); 
                lock_.unlock(); 
                return true; 
            }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) 
                return true;
        }
    }

    bool dequeue(T* out, int tid) {
        uint64_t deq_inv_ts = now();

        auto& slot = operations_[tid];
        slot.node.value = T{};
        slot.success = false;
        slot.node.deq_inv_ts = deq_inv_ts; // Pass invocation time to combiner
        slot.opcode.store(Opcode::Dequeue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { 
                scan_combine_apply(); 
                lock_.unlock(); 
                break; 
            }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) 
                break;
        }

        if (slot.success) {
            *out = slot.node.value; 

            // ---------------------------------------------------
            // LOGGING HAPPENS HERE (Outside Critical Section)
            // ---------------------------------------------------
            // The requestor thread logs its own operation. 
            // The Combiner just filled in the timestamps for us in 'slot.node'.
            fc_tls_log.events.emplace_back(
                slot.node.enq_inv_ts,
                slot.node.enq_lin_ts,
                slot.node.deq_inv_ts, // This was ours
                slot.node.deq_lin_ts  // This was set by combiner
            );
        }
        
        return slot.success;
    }

    // Merges thread local logs into the global list
    void commit_thread_logs() {
        if (fc_tls_log.events.empty()) return;
        std::lock_guard<std::mutex> lg(records_mutex);
        records.insert(records.end(), fc_tls_log.events.begin(), fc_tls_log.events.end());
        fc_tls_log.events.clear();
    }

private:
    void scan_combine_apply() {
        for (size_t i = 0; i < operations_.size(); ++i) {
            auto& slot = operations_[i];
            Opcode code = slot.opcode.load(std::memory_order_acquire);

            if (code == Opcode::Enqueue) {
                FCNode<T> n = slot.node;
                n.enq_lin_ts = now(); // Linearization point

                q_.push(n);
                slot.opcode.store(Opcode::Done, std::memory_order_release);
            }
            else if (code == Opcode::Dequeue) {
                if (!q_.empty()) {
                    FCNode<T> n = q_.front(); 
                    uint64_t deq_lin_ts = now(); // Linearization point
                    q_.pop();

                    // CRITICAL CHANGE:
                    // Instead of logging to vector here (slow!), we pass the data 
                    // back to the requestor via the slot.
                    slot.node.value = n.value;
                    slot.node.enq_inv_ts = n.enq_inv_ts; // Pass back history
                    slot.node.enq_lin_ts = n.enq_lin_ts; // Pass back history
                    slot.node.deq_lin_ts = deq_lin_ts;   // Pass back linearization time
                    
                    slot.success = true;
                } else {
                    slot.success = false;
                }
                slot.opcode.store(Opcode::Done, std::memory_order_release);
            }
        }
    }

    std::vector<Operation<T>> operations_;
    std::queue<FCNode<T>>     q_;
    std::mutex                lock_;
};