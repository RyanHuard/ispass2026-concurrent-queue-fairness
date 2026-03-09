#pragma once

#include <atomic>
#include <vector>
#include <tuple>
#include <mutex>
#include <thread>
#include <iostream>
#include "LinkedRingQueue.hpp"
#include "RQCell.hpp"
#include "x86AtomicOps.hpp"
#include "CacheRemap.hpp"
#include <immintrin.h> 


// private void burn_cycles(int iterations) {
//     volatile int sink = 0; // 'volatile' prevents optimization
//     for (int i = 0; i < iterations; ++i) {
//         sink++; 
//         // A hardware hint to the CPU that we are spinning (saves power/heat)
//         // usage: _mm_pause() (Intel/AMD) or __yield() (ARM)
//         #if defined(__x86_64__) || defined(_M_X64)
//             _mm_pause(); 
//         #endif
//     }
// }


// -----------------------------------------------------------
// 1. Data Structures & Helper Types
// -----------------------------------------------------------

// Wrapper node that carries the user value + timestamps
template <typename T>
struct Node {
    T value;
    uint64_t enq_inv_ts = 0;
    uint64_t enq_lin_ts = 0;
    uint64_t deq_inv_ts = 0;
    uint64_t deq_lin_ts = 0;
};

// Record: Enq Invocation, Enq Linearization, Deq Invocation, Deq Linearization
using LogRecord = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;

// -----------------------------------------------------------
// 2. Global Thread Local Storage
// -----------------------------------------------------------
// We move this OUT of the template class to ensure it is always 
// instantiated correctly and accessible.
struct ThreadLogStorage {
    std::vector<LogRecord> events;
    ThreadLogStorage() {
        events.reserve(2000000); 
    }
};

// This ensures every thread in the process gets its own log
static thread_local ThreadLogStorage g_tls_log;

// -----------------------------------------------------------
// 3. The LCRQ Class (Unchanged logic, just wrapper)
// -----------------------------------------------------------

template<typename NodeT, bool padded_cells, size_t ring_size, bool cache_remap>
class CRQueue : public QueueSegmentBase<NodeT, CRQueue<NodeT, padded_cells, ring_size, cache_remap>> {
private:
    using Base = QueueSegmentBase<NodeT, CRQueue<NodeT, padded_cells, ring_size, cache_remap>>;
    using Cell = detail::CRQCell<NodeT*, padded_cells>;

    Cell array[ring_size];
    [[no_unique_address]] ConditionalCacheRemap<cache_remap, ring_size, sizeof(Cell)> remap{};

    inline uint64_t node_index(uint64_t i) const { return (i & ~(1ull << 63)); }
    inline uint64_t set_unsafe(uint64_t i) const { return (i | (1ull << 63)); }
    inline uint64_t node_unsafe(uint64_t i) const { return (i & (1ull << 63)); }

public:
    static constexpr size_t RING_SIZE = ring_size;

    CRQueue(uint64_t start) : Base() {
        for (uint64_t i = start; i < start + RING_SIZE; i++) {
            uint64_t j = i % RING_SIZE;
            array[remap[j]].val.store(nullptr, std::memory_order_relaxed);
            array[remap[j]].idx.store(i, std::memory_order_relaxed);
        }
        Base::head.store(start, std::memory_order_relaxed);
        Base::tail.store(start, std::memory_order_relaxed);
    }

    static std::string className() {
        return "CRQueue_TLS";
    }

    bool enqueue(NodeT* item, [[maybe_unused]] const int tid) {
        int try_close = 0;
        while (true) {
            uint64_t tailticket = Base::tail.fetch_add(1);

            if (Base::isClosed(tailticket)) return false;

            Cell& cell = array[remap[tailticket % RING_SIZE]];
            uint64_t idx = cell.idx.load();
            if (cell.val.load() == nullptr) {
                if (node_index(idx) <= tailticket) {
                    if ((!node_unsafe(idx) || Base::head.load() < tailticket)) {
                        if (CAS2((void**)&cell, nullptr, idx, item, tailticket)) {
                            item->enq_lin_ts = now(); // Capture Lin Time
                            return true;
                        }
                    }
                }
            }
            if (tailticket >= Base::head.load() + RING_SIZE) {
                if (Base::closeSegment(tailticket, ++try_close > 10)) return false;
            }
        }
    }

    NodeT* dequeue([[maybe_unused]] const int tid) {
#ifdef CAUTIOUS_DEQUEUE
        if (Base::isEmpty()) return nullptr;
#endif
        while (true) {
            uint64_t headticket = Base::head.fetch_add(1);
            Cell& cell = array[remap[headticket % RING_SIZE]];

            int r = 0;
            uint64_t tt = 0;

            while (true) {
                uint64_t cell_idx = cell.idx.load();
                uint64_t unsafe   = node_unsafe(cell_idx);
                uint64_t idx      = node_index(cell_idx);
                NodeT* val        = static_cast<NodeT*>(cell.val.load());

                if (idx > headticket) break;

                if (val != nullptr) {
                    if (idx == headticket) {
                        if (CAS2((void**)&cell, val, cell_idx, nullptr, unsafe | (headticket + RING_SIZE))) {
                            val->deq_lin_ts = now(); // Capture Lin Time
                            return val;
                        }
                    } else {
                        if (CAS2((void**)&cell, val, cell_idx, val, set_unsafe(idx))) break;
                    }
                } else {
                    if ((r & ((1ull << 8) - 1)) == 0) tt = Base::tail.load();
                    int crq_closed = Base::isClosed(tt);
                    uint64_t t = Base::tailIndex(tt);
                    if (unsafe || t < headticket + 1 || crq_closed || r > 4*1024) {
                        if (CAS2((void**)&cell, val, cell_idx, val, unsafe | (headticket + RING_SIZE))) break;
                    }
                    ++r;
                }
            }
            if (Base::tailIndex(Base::tail.load()) <= headticket + 1) {
                Base::fixState();
                return nullptr;
            }
        }
    }
};

template<typename T, bool padded_cells = false, size_t ring_size = 1024, bool cache_remap = true>
using LCRQueue_Internal = LinkedRingQueue<T, CRQueue<T, padded_cells, ring_size, cache_remap>>;


// -----------------------------------------------------------
// 4. The Adapter (Interface for Benchmark)
// -----------------------------------------------------------

template<typename T, bool P, size_t R, bool C>
struct CRQueueAdapter {
    using NodeT = Node<T>;
    using Underlying = LCRQueue_Internal<NodeT, P, R, C>;
    
    Underlying q;
    std::mutex records_mutex;
    std::vector<LogRecord> records;

    CRQueueAdapter() = default;

    bool enqueue(const T v, int tid) {
        NodeT* ptr = new NodeT{v, 0, 0, 0, 0};
        ptr->enq_inv_ts = now(); 
        q.enqueue(ptr, tid);
        return true;
    }

    bool dequeue(T* out, int tid) {
        NodeT* ptr = q.dequeue(tid);
        if (!ptr) return false;

        *out = ptr->value;
        uint64_t current_deq_inv = now();

        // Log to GLOBAL thread local storage
        g_tls_log.events.emplace_back(
            ptr->enq_inv_ts,
            ptr->enq_lin_ts,
            current_deq_inv,
            ptr->deq_lin_ts
        );

        delete ptr;
        return true;
    }

    void commit_thread_logs() {
        if (g_tls_log.events.empty()) return;

        std::lock_guard<std::mutex> lg(records_mutex);
        records.insert(records.end(), g_tls_log.events.begin(), g_tls_log.events.end());
        g_tls_log.events.clear();
    }
};