#ifndef _FAA_ARRAY_QUEUE_HP_H_
#define _FAA_ARRAY_QUEUE_HP_H_

#include <atomic>
#include <cstring>
#include <stdexcept>
#include <cmath>
#include "RQCell.hpp"
#include "HazardPointers.hpp"
#include "CacheRemap.hpp"
#include "x86AtomicOps.hpp"   


template<typename T, bool padded_cells = true, int BUFFER_SIZE = 1024, bool cache_remap = true>
class FAAArrayQueue {
private:
    using Cell = detail::PlainCell<T*, padded_cells>;
    constexpr static ConditionalCacheRemap<cache_remap, BUFFER_SIZE, sizeof(Cell)> remap{};

    struct Node {
        alignas(128) std::atomic<int>   deqidx;
        alignas(128) std::atomic<int>   enqidx;
        alignas(128) std::atomic<Node*> next;
        Cell                            items[BUFFER_SIZE];
        const uint64_t                  startIndexOffset;

        // Start with the first entry pre-filled and enqidx at 1
        Node(T* item, uint64_t startIndexOffset)
            : deqidx{0}, enqidx{1}, next{nullptr},
              startIndexOffset(startIndexOffset) {
            std::memset(items, 0, sizeof(items));
            items[remap[0]].val.store(item, std::memory_order_relaxed);
        }

        bool casNext(Node *cmp, Node *val) {
            return next.compare_exchange_strong(cmp, val);
        }
    };

    bool casTail(Node *cmp, Node *val) {
        return tail.compare_exchange_strong(cmp, val);
    }

    bool casHead(Node *cmp, Node *val) {
        return head.compare_exchange_strong(cmp, val);
    }

    // Pointers to head and tail of the list
    alignas(128) std::atomic<Node*> head;
    alignas(128) std::atomic<Node*> tail;

    static constexpr int MAX_THREADS = 128;
    const int maxThreads;

    T* taken = (T*)new int();  // Muuuahahah !

    // We need just one hazard pointer
    HazardPointers<Node> hp {2, maxThreads};
    const int kHpTail = 0;
    const int kHpHead = 1;

public:
    static constexpr size_t RING_SIZE = BUFFER_SIZE;

    FAAArrayQueue(int maxThreads=MAX_THREADS) :
    maxThreads(maxThreads) {
        Node* sentinelNode = new Node(nullptr, 0);
        sentinelNode->enqidx.store(0, std::memory_order_relaxed);
        head.store(sentinelNode, std::memory_order_relaxed);
        tail.store(sentinelNode, std::memory_order_relaxed);
    }

    ~FAAArrayQueue() {
        while (dequeue(0) != nullptr); // Drain the queue
        delete head.load();            // Delete the last node
        delete (int*)taken;
    }

    static std::string className() {
        using namespace std::string_literals;
        return "FAAArrayQueue"s + (padded_cells ? "/ca"s : ""s)
                                + (cache_remap ? "/remap" : "");
    }

    size_t estimateSize(int tid) {
        Node* lhead = hp.protect(kHpHead, head, tid);
        Node* ltail = hp.protect(kHpTail, tail, tid);
        uint64_t t = std::min((uint64_t) RING_SIZE,
                              (uint64_t) ltail->enqidx.load())
                     + ltail->startIndexOffset;
        uint64_t h = std::min((uint64_t) RING_SIZE,
                              (uint64_t) lhead->deqidx.load())
                     + lhead->startIndexOffset;
        hp.clear(tid);
        return t > h ? t - h : 0;
    }

    // T is typically Node<Payload> in your fairness harness, so we can
    // safely write item->enq_lin_ts / deq_lin_ts when instantiated that way.
    void enqueue(T* item, const int tid) {
        if (item == nullptr)
            throw std::invalid_argument("item can not be nullptr");

        while (true) {
            Node* ltail = hp.protect(kHpTail, tail, tid);
            const int idx = ltail->enqidx.fetch_add(1);
            if (idx > BUFFER_SIZE-1) { // This node is full
                if (ltail != tail.load()) continue;
                Node* lnext = ltail->next.load();
                if (lnext == nullptr) {
                    Node* newNode = new Node(item, ltail->startIndexOffset + BUFFER_SIZE);
                    if (ltail->casNext(nullptr, newNode)) {
                        // Linearization for this enqueue is when the new
                        // node becomes reachable from the list (casNext)
                        item->enq_lin_ts = now();
                        casTail(ltail, newNode);
                        hp.clearOne(kHpTail, tid);
                        return;
                    }

                } else {
                    casTail(ltail, lnext);
                }
                continue;
            }

            T* itemnull = nullptr;
            if (ltail->items[remap[idx]].val.compare_exchange_strong(itemnull, item)) {
                // Linearization for this enqueue is the successful CAS
                item->enq_lin_ts = now();
                hp.clearOne(kHpTail, tid);
                return;
            }
        }
    }

    T* dequeue(const int tid) {
        T*   item  = nullptr;
        Node* lhead = hp.protect(kHpHead, head, tid);

#ifdef CAUTIOUS_DEQUEUE
        if (lhead->deqidx.load() >= lhead->enqidx.load()
            && lhead->next.load() == nullptr)
            return nullptr;
#endif

        while (true) {
            const int idx = lhead->deqidx.fetch_add(1);
            if (idx > BUFFER_SIZE-1) { // This node has been drained
                Node* lnext = lhead->next.load();
                if (lnext == nullptr) {
                    break;
                }
                if (casHead(lhead, lnext))
                    hp.retire(lhead, tid);

                lhead = hp.protect(kHpHead, head, tid);
                continue;
            }

            Cell& cell = lhead->items[remap[idx]];
            if (cell.val.load() == nullptr && idx < lhead->enqidx.load()) {
                for (size_t i = 0; i < 4*1024; ++i) {
                    if (cell.val.load() != nullptr)
                        break;
                }
            }

            item = cell.val.exchange(taken);
            if (item != nullptr) {
                // Linearization point for dequeue: successful exchange to 'taken'
                item->deq_lin_ts = now();
                break;
            }

            int t = lhead->enqidx.load();
            if (idx + 1 >= t) {
                if (lhead->next.load() != nullptr)
                    continue;
                lhead->enqidx.compare_exchange_strong(t, idx + 1);
                break;
            }
        }

        hp.clearOne(kHpHead, tid);
        return item;
    }
};


// ------------------------------------------------------------------
// Adapter for fairness benchmarking, mirroring CRQueueAdapter/PRQueueAdapter
// ------------------------------------------------------------------
template<typename T, bool P=true, int BUFFER_SIZE=1024, bool C=true>
struct FAAArrayQueueAdapter {
    using NodeT      = Node<T>;  // your global timestamped Node<T>
    using Underlying = FAAArrayQueue<NodeT, P, BUFFER_SIZE, C>;

    Underlying             q;
    std::mutex             records_mutex;
    std::vector<LogRecord> records;

    FAAArrayQueueAdapter(int maxThreads)
        : q(maxThreads) {}

    bool enqueue(const T v, int tid) {
        NodeT* ptr = new NodeT{v, 0, 0, 0, 0};
        ptr->enq_inv_ts = now();
        q.enqueue(ptr, tid);
        return true;
    }

    bool dequeue(T* out, int tid) {
        uint64_t inv = now();
        NodeT* ptr = q.dequeue(tid);
        if (!ptr) return false;

        *out = ptr->value;

        ptr->deq_inv_ts = inv;

        g_tls_log.events.emplace_back(
            ptr->enq_inv_ts,
            ptr->enq_lin_ts,
            ptr->deq_inv_ts,
            ptr->deq_lin_ts
        );

        delete ptr;
        return true;
    }

    void commit_thread_logs() {
        if (g_tls_log.events.empty()) return;
        std::lock_guard<std::mutex> lg(records_mutex);
        records.insert(records.end(),
                       g_tls_log.events.begin(),
                       g_tls_log.events.end());
        g_tls_log.events.clear();
    }
};

#endif /* _FAA_ARRAY_QUEUE_HP_H_ */
