#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <iostream>
#include <cstdint>
#include <limits>



static constexpr int EMPTY_QUEUE = std::numeric_limits<int>::min();

template <typename T>
class SySQueue {
public:
    std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> records;
    explicit SySQueue(std::size_t nThreads)
        : ThreadDummyCommands_(nThreads) {
        // --- data queue ---
        auto* p = new Node(); // initial dummy (next==self)
        NQPos_.store(p, std::memory_order_relaxed);
        DQPos_.store(p, std::memory_order_relaxed);

        // --- command nodes: allocate nThreads + 1 unique nodes ---
        AllCommands_.reserve(nThreads + 1);
        for (std::size_t i = 0; i <= nThreads; ++i) AllCommands_.push_back(new Cmd());

        // per-thread "next dummy": size nThreads, initial unique node each
        for (std::size_t i = 0; i < nThreads; ++i) ThreadDummyCommands_[i] = AllCommands_[i];

        // sentinel/head for command ring is the last unique node
        Cmd* sentinel = AllCommands_.back();
        DQCmdPos_.store(sentinel, std::memory_order_relaxed);
        NQCmdPos_.store(sentinel, std::memory_order_relaxed);
        sentinel->Next.store(sentinel, std::memory_order_relaxed);

        // start proxy
        NeedDQProxy_.store(true, std::memory_order_release);
        proxy_thread_ = std::thread([this]() { proxy_loop_(); });
    }

    ~SySQueue() {
        // stop proxy
        NeedDQProxy_.store(false, std::memory_order_release);
        if (proxy_thread_.joinable()) proxy_thread_.join();

        // free ALL command nodes exactly once
        for (Cmd* c : AllCommands_) delete c;
        AllCommands_.clear();
        ThreadDummyCommands_.clear();
        DQCmdPos_.store(nullptr, std::memory_order_relaxed);
        NQCmdPos_.store(nullptr, std::memory_order_relaxed);

        // walk data queue and free remaining nodes (typically final dummy)
        Node* start = DQPos_.load(std::memory_order_acquire);
        if (start) {
            Node* next = start->next.load(std::memory_order_relaxed);
            if (next == start) {
                delete start;
            } else {
                start->next.store(nullptr, std::memory_order_relaxed); // break cycle
                Node* cur = start;
                while (cur) {
                    Node* tmp = cur->next.load(std::memory_order_relaxed);
                    delete cur;
                    cur = tmp;
                }
            }
        }
        DQPos_.store(nullptr, std::memory_order_relaxed);
        NQPos_.store(nullptr, std::memory_order_relaxed);
    }

    SySQueue(const SySQueue&) = delete;
    SySQueue& operator=(const SySQueue&) = delete;
    SySQueue(SySQueue&&) = delete;
    SySQueue& operator=(SySQueue&&) = delete;

    // ---------- enqueue (single CAS publish via tail-swap) ----------
    inline void enqueue(T arg, int tid) {
        auto* p = new Node();              // fresh empty node (next==self)
        uint64_t call_ts = adj_now();

        std::atomic_thread_fence(std::memory_order_seq_cst);
        Node* oldTail = NQPos_.exchange(p, std::memory_order_acq_rel);

        oldTail->val = arg;
        std::atomic_thread_fence(std::memory_order_release);
        oldTail->next.store(p, std::memory_order_release);

        uint64_t in_ts = adj_now();
        oldTail->call_ts = call_ts;
        oldTail->in_ts   = in_ts;
    }

    // ---------- dequeue via proxy (tid in [0, nThreads-1]) ----------
    inline bool dequeue(T* out, int tid) {
        Cmd* NewDummyTail = ThreadDummyCommands_[tid];
        NewDummyTail->Next.store(NewDummyTail, std::memory_order_relaxed);
        NewDummyTail->pid = tid;
        NewDummyTail->ready.store(false, std::memory_order_relaxed);
        NewDummyTail->had_value = false;

        // publish command by swapping tail
        Cmd* Current = NQCmdPos_.exchange(NewDummyTail, std::memory_order_acq_rel);
        Current->Next.store(NewDummyTail, std::memory_order_release);

        // rotate this thread's "next dummy" to old tail (the command node)
        ThreadDummyCommands_[tid] = Current;

        // wait for proxy response
        while (!Current->ready.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        if (!Current->had_value) return false;
        *out = Current->out_val;
        dequeue_counter_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }


private:
    struct Node {
        std::atomic<Node*> next{nullptr};
        T             val{EMPTY_QUEUE};
        uint64_t call_ts{0}, in_ts{0}, deq_ts{0};
        int      tid{-1};
        Node() { next.store(this, std::memory_order_relaxed); } // self-loop
    };

    struct Cmd {
        std::atomic<Cmd*> Next{nullptr}; // circular list
        int      pid{0};
        std::atomic<bool> ready{false};
        bool     had_value{false};
        T   out_val{EMPTY_QUEUE};
        Cmd() { Next.store(this, std::memory_order_relaxed); }
    };

    // ---------- proxy loop ----------
    void proxy_loop_() {
        while (NeedDQProxy_.load(std::memory_order_acquire)) {
            Cmd* head = DQCmdPos_.load(std::memory_order_acquire);
            Cmd* next = head->Next.load(std::memory_order_acquire);

            if (next == head) { // no command
                std::this_thread::yield();
                continue;
            }

            // pop command (head is current command)
            Cmd* current = head;
            DQCmdPos_.store(next, std::memory_order_release);

            // serve: dequeue one node from data queue
            Node* oldHead;
            Node* dnxt;
            bool got = false;
            T out = EMPTY_QUEUE;
            uint64_t deq_ts = 0, in_ts = 0, call_ts = 0;

            while (true) {
                oldHead = DQPos_.load(std::memory_order_acquire);
                dnxt    = oldHead->next.load(std::memory_order_acquire);

                if (dnxt == oldHead) { got = false; break; } // empty

                if (DQPos_.compare_exchange_weak(oldHead, dnxt,
                                                 std::memory_order_acq_rel,
                                                 std::memory_order_acquire)) {
                    out     = oldHead->val;
                    deq_ts  = adj_now();
                    call_ts = oldHead->call_ts;
                    in_ts   = oldHead->in_ts;
                    got     = true;

                    oldHead->tid    = current->pid;
                    oldHead->deq_ts = deq_ts;

                    // log triple
                    {
                        std::lock_guard<std::mutex> lk(rec_mtx_);
                        records.emplace_back(call_ts, in_ts, deq_ts);
                    }

                    delete oldHead; // only proxy deletes data nodes
                    break;
                }
            }

            // respond
            current->out_val   = got ? out : EMPTY_QUEUE;
            current->had_value = got;
            current->ready.store(true, std::memory_order_release);
        }
    }

private:
    // data queue
    std::atomic<Node*> NQPos_{nullptr}; // tail
    std::atomic<Node*> DQPos_{nullptr}; // head

    // command queue (proxy)
    std::atomic<Cmd*> DQCmdPos_{nullptr};
    std::atomic<Cmd*> NQCmdPos_{nullptr};
    std::vector<Cmd*> ThreadDummyCommands_; // per-thread rotating "next dummy"
    std::vector<Cmd*> AllCommands_;         // UNIQUE list for destruction

    // proxy thread control
    std::atomic<bool> NeedDQProxy_{false};
    std::thread proxy_thread_;

    // stats
    std::atomic<std::uint64_t> enqueue_counter_{0};
    std::atomic<std::uint64_t> dequeue_counter_{0};

    // logging
    std::mutex rec_mtx_;
};
