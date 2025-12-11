#pragma once

#include <atomic>
#include <vector>
#include <tuple>
#include <mutex>

#include "LinkedRingQueue.hpp"
#include "RQCell.hpp"
#include "CacheRemap.hpp"
#include "x86AtomicOps.hpp"   // for now()

// Assumes:
//   template <typename T> struct Node { T value; uint64_t enq_inv_ts, enq_lin_ts, deq_inv_ts, deq_lin_ts; };
//   using LogRecord = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;
//   struct ThreadLogStorage { std::vector<LogRecord> events; ... };
//   extern thread_local ThreadLogStorage g_tls_log;

// -----------------------------------------------------------
// 1. PRQueue segment (instrumented with lin timestamps)
// -----------------------------------------------------------

template<typename T, bool padded_cells, size_t ring_size, bool cache_remap>
class PRQueue : public QueueSegmentBase<T, PRQueue<T, padded_cells, ring_size, cache_remap>> {
private:
    using Base = QueueSegmentBase<T, PRQueue<T, padded_cells, ring_size, cache_remap>>;
    using Cell = detail::CRQCell<void*, padded_cells>;

    Cell array[ring_size];

    [[no_unique_address]] ConditionalCacheRemap<cache_remap, ring_size, sizeof(Cell)> remap{};

    inline uint64_t nodeIndex(uint64_t i) const {
        return (i & ~(1ull << 63));
    }

    inline uint64_t setUnsafe(uint64_t i) const {
        return (i | (1ull << 63));
    }

    inline uint64_t nodeUnsafe(uint64_t i) const {
        return (i & (1ull << 63));
    }

    inline bool isBottom(void* const value) const {
        return (reinterpret_cast<uintptr_t>(value) & 1) != 0;
    }

    inline void* threadLocalBottom(const int tid) const {
        return reinterpret_cast<void*>(static_cast<uintptr_t>((tid << 1) | 1));
    }

public:
    static constexpr size_t RING_SIZE = ring_size;

    PRQueue(uint64_t start) : Base() {
        for (uint64_t i = start; i < start + RING_SIZE; i++) {
            uint64_t j = i % RING_SIZE;
            array[remap[j]].val.store(nullptr, std::memory_order_relaxed);
            array[remap[j]].idx.store(i, std::memory_order_relaxed);
        }
        Base::head.store(start, std::memory_order_relaxed);
        Base::tail.store(start, std::memory_order_relaxed);
    }

    static std::string className() {
        using namespace std::string_literals;
        return "PRQueue"s + (padded_cells ? "/ca"s : ""s) + (cache_remap ? "/remap" : "");
    }

    // T is the "node" type (e.g., Node<int>), the queue stores T*
    bool enqueue(T* item, const int tid) {
        int try_close = 0;

        while (true) {
            uint64_t tailticket = Base::tail.fetch_add(1);
            if (Base::isClosed(tailticket)) {
                return false;
            }

            Cell& cell = array[remap[tailticket % RING_SIZE]];
            uint64_t idx = cell.idx.load();
            void* val = cell.val.load();

            if (val == nullptr
                && nodeIndex(idx) <= tailticket
                && (!nodeUnsafe(idx) || Base::head.load() <= tailticket)) {

                void* bottom = threadLocalBottom(tid);
                if (cell.val.compare_exchange_strong(val, bottom)) {
                    if (cell.idx.compare_exchange_strong(idx, tailticket + RING_SIZE)) {
                        if (cell.val.compare_exchange_strong(bottom, item)) {
                            // Successful linearization of enqueue
                            item->enq_lin_ts = now();
                            return true;
                        }
                    } else {
                        cell.val.compare_exchange_strong(bottom, nullptr);
                    }
                }
            }

            if (tailticket >= Base::head.load() + RING_SIZE) {
                if (Base::closeSegment(tailticket, ++try_close > 10))
                    return false;
            }
        }
    }

    T* dequeue([[maybe_unused]] const int tid) {
#ifdef CAUTIOUS_DEQUEUE
        if (Base::isEmpty())
            return nullptr;
#endif

        while (true) {
            uint64_t headticket = Base::head.fetch_add(1);
            Cell& cell = array[remap[headticket % RING_SIZE]];

            int r = 0;
            uint64_t tt = 0;

            while (true) {
                uint64_t cell_idx = cell.idx.load();
                uint64_t unsafe = nodeUnsafe(cell_idx);
                uint64_t idx = nodeIndex(cell_idx);
                void* val = cell.val.load();

                if (idx > headticket + RING_SIZE)
                    break;

                if (val != nullptr && !isBottom(val)) {
                    if (idx == headticket + RING_SIZE) {
                        // We are the linearizing dequeuer
                        cell.val.store(nullptr);
                        T* node = static_cast<T*>(val);
                        node->deq_lin_ts = now();
                        return node;
                    } else {
                        if (unsafe) {
                            if (cell.idx.load() == cell_idx)
                                break;
                        } else {
                            if (cell.idx.compare_exchange_strong(cell_idx, setUnsafe(idx)))
                                break;
                        }
                    }
                } else {
                    if ((r & ((1ull << 8) - 1)) == 0)
                        tt = Base::tail.load();

                    int crq_closed = Base::isClosed(tt);
                    uint64_t t = Base::tailIndex(tt);
                    if (unsafe || t < headticket + 1 || crq_closed || r > 4*1024) {
                        if (isBottom(val) && !cell.val.compare_exchange_strong(val, nullptr))
                            continue;
                        if (cell.idx.compare_exchange_strong(cell_idx, unsafe | (headticket + RING_SIZE)))
                            break;
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

// Same general alias; for benchmarking we will instantiate with T = Node<Payload>
template<typename T, bool padded_cells = false, size_t ring_size = 1024, bool cache_remap = true>
using LPRQueue = LinkedRingQueue<T, PRQueue<T, padded_cells, ring_size, cache_remap>>;


// -----------------------------------------------------------
// 2. Adapter for Benchmark Harness (like CRQueueAdapter)
// -----------------------------------------------------------

template<typename T, bool P, size_t R, bool C>
struct PRQueueAdapter {
    using NodeT     = Node<T>;
    using Underlying = LPRQueue<NodeT, P, R, C>;

    Underlying            q;
    std::mutex            records_mutex;
    std::vector<LogRecord> records;

    PRQueueAdapter() = default;

    bool enqueue(const T v, int tid) {
        auto* ptr = new NodeT{v, 0, 0, 0, 0};
        ptr->enq_inv_ts = now();
        q.enqueue(ptr, tid);
        return true;
    }

    bool dequeue(T* out, int tid) {
        NodeT* ptr = q.dequeue(tid);
        if (!ptr) return false;

        *out = ptr->value;
        uint64_t current_deq_inv = now();

        // Log to thread-local buffer
        g_tls_log.events.emplace_back(
            ptr->enq_inv_ts,
            ptr->enq_lin_ts,
            current_deq_inv,
            ptr->deq_lin_ts
        );

        delete ptr;
        return true;
    }

    // To be called once per thread at the end of a run
    void commit_thread_logs() {
        if (g_tls_log.events.empty()) return;

        std::lock_guard<std::mutex> lg(records_mutex);
        records.insert(records.end(),
                       g_tls_log.events.begin(),
                       g_tls_log.events.end());
        g_tls_log.events.clear();
    }
};
