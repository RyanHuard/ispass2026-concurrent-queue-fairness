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

// Node carried through the queue; holds per-element timestamps until pop.
template <typename T>
struct FCNode {
    T        value{};
    uint64_t call_ts{0};  // set at enqueue() call
    uint64_t in_ts{0};    // set at enqueue LP (push)
    uint64_t deq_ts{0};   // set at dequeue LP (pop)
};

template <typename T>
struct alignas(64) Operation {
    std::atomic<Opcode> opcode{Opcode::Done};
    FCNode<T>           node{};   // payload/result for this thread
};

template <typename T>
class FlatCombiningQueue {
public:
    // Exactly like your MS queue:
    // (call_ts, in_ts, deq_ts) — pushed ONLY when the element is popped.
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> records;

    explicit FlatCombiningQueue(size_t num_threads)
        : operations_(num_threads) {}

    // API unchanged: enqueue a value, pass thread id explicitly.
    bool enqueue(const T& item, int tid) {
        auto& slot = operations_[tid];
        slot.node.value   = item;
        slot.node.call_ts = now();                       // (1) call time
        slot.node.in_ts   = 0;
        slot.node.deq_ts  = 0;
        slot.opcode.store(Opcode::Enqueue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { scan_combine_apply(); lock_.unlock(); return true; }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) return true;
        }
    }

    bool dequeue(T* out, int tid) {
        auto& slot = operations_[tid];
        slot.node = {}; // clear request/result
        slot.opcode.store(Opcode::Dequeue, std::memory_order_release);

        while (true) {
            if (lock_.try_lock()) { scan_combine_apply(); lock_.unlock(); break; }
            if (slot.opcode.load(std::memory_order_acquire) == Opcode::Done) break;
        }

        *out = slot.node.value; // T{} if queue empty at LP
        return true;
    }

private:
    void scan_combine_apply() {
        for (size_t i = 0; i < operations_.size(); ++i) {
            auto& slot = operations_[i];
            Opcode code = slot.opcode.load(std::memory_order_acquire);

            if (code == Opcode::Enqueue) {
                // Enqueue LP: element becomes visible
                FCNode<T> n = slot.node;          // copy out of the slot
                n.in_ts = now();                  // (2) insertion LP
                q_.push(n);
                slot.opcode.store(Opcode::Done, std::memory_order_release);
                slot.node = {};                   // clear slot
            }
            else if (code == Opcode::Dequeue) {
                if (!q_.empty()) {
                    FCNode<T> n = q_.front(); q_.pop();
                    n.deq_ts = now();            // (3) dequeue LP

                    // Log ONLY now (at pop):
                    records.emplace_back(n.call_ts, n.in_ts, n.deq_ts);

                    // return value to the requester
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
