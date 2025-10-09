// fc_queue_lps_per_element.cpp
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

using namespace std;

enum class Opcode : uint8_t { Done = 0, Enqueue = 1, Dequeue = 2 };

template <typename T>
struct FCNode {
    T        value{};

    uint64_t enq_inv_ts;
    uint64_t enq_lin_ts;
    uint64_t deq_inv_ts;
    uint64_t deq_lin_ts;
};

template <typename T>
struct alignas(64) Operation {
    std::atomic<Opcode> opcode{Opcode::Done};
    FCNode<T>           node{};  
};

template <typename T>
class FlatCombiningQueue {
public:
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> records;

    explicit FlatCombiningQueue(size_t num_threads)
        : operations_(num_threads) {}

    bool enqueue(const T& item, int tid) {
        uint64_t enq_inv_ts = now();

        auto& slot = operations_[tid];
        slot.node.enq_inv_ts = enq_inv_ts;
        slot.node.value   = item;
        slot.opcode.store(Opcode::Enqueue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { scan_combine_apply(); lock_.unlock(); return true; }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) return true;
        }
    }

    bool dequeue(T* out, int tid) {
        uint64_t deq_inv_ts = now();

        auto& slot = operations_[tid];
        slot.node = {}; 
        slot.opcode.store(Opcode::Dequeue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { 
                scan_combine_apply(deq_inv_ts); 
                lock_.unlock(); 
                break; 
            }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) break;
        }

        *out = slot.node.value; 
        return true;
    }

private:
    void scan_combine_apply(uint64_t deq_inv_ts = 0) {
        for (size_t i = 0; i < operations_.size(); ++i) {
            auto& slot = operations_[i];
            Opcode code = slot.opcode.load(std::memory_order_acquire);

            if (code == Opcode::Enqueue) {
                FCNode<T> n = slot.node;

                n.enq_lin_ts = now(); 
                
                q_.push(n);
                slot.opcode.store(Opcode::Done, std::memory_order_release);
                slot.node = {};
            }
            else if (code == Opcode::Dequeue) {
                if (!q_.empty()) {
                    FCNode<T> n = q_.front(); 
                    q_.pop();

                    n.deq_lin_ts = now(); 

                    records.emplace_back(n.enq_inv_ts, n.enq_lin_ts, deq_inv_ts, n.deq_lin_ts);

                    slot.node.value = n.value;
                } else {
                    slot.node.value = T{};
                }
                slot.opcode.store(Opcode::Done, std::memory_order_release);
            }
        }
    }

    std::vector<Operation<T>> operations_;
    std::queue<FCNode<T>>     q_;
    std::mutex                lock_;
};
