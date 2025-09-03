#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include <mutex>
#include <iostream>
#include <cstdint>
#include <limits>

#include "FairnessLogger.hpp"

// ---------- time & (optional) logging ----------

struct Record_2 {
    int    tid;
    double call_ts, in_ts, deq_ts;
};
static std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> g_records;
static std::mutex          g_rec_mtx;
static inline void log_append(uint64_t call_ts, uint64_t in_ts, uint64_t deq_ts) {
    std::lock_guard<std::mutex> lk(g_rec_mtx);
    g_records.emplace_back(call_ts, in_ts, deq_ts);
}

// ---------- queue types ----------
using ArgVal = std::int64_t;
static constexpr ArgVal EMPTY_QUEUE = std::numeric_limits<ArgVal>::min();

struct SySQNode {
    std::atomic<SySQNode*> next{nullptr};
    ArgVal                 val{EMPTY_QUEUE};
    uint64_t call_ts{0}, in_ts{0}, deq_ts{0};
    int    tid{-1};
    SySQNode() { next.store(this, std::memory_order_relaxed); } // self-loop
};

struct tDQCommand {
    std::atomic<tDQCommand*> Next{nullptr}; // circular list
    int      pid{0};
    std::atomic<bool> ready{false};
    bool     had_value{false};
    ArgVal   out_val{EMPTY_QUEUE};
    tDQCommand() { Next.store(this, std::memory_order_relaxed); }
};

struct SySQueueStruct {
    // data queue
    std::atomic<SySQNode*> NQPos{nullptr}; // tail
    std::atomic<SySQNode*> DQPos{nullptr}; // head

    // command queue (proxy)
    std::atomic<tDQCommand*> DQCmdPos{nullptr};
    std::atomic<tDQCommand*> NQCmdPos{nullptr};
    std::vector<tDQCommand*> ThreadDummyCommands; // per-thread rotating "next dummy"
    std::vector<tDQCommand*> AllCommands;         // UNIQUE list for destruction

    // proxy thread control
    std::atomic<bool> NeedDQProxy{false};
    std::thread proxy_thread;

    // stats
    std::atomic<std::uint64_t> enqueue_counter{0};
    std::atomic<std::uint64_t> dequeue_counter{0};
};

// ---------- init / start proxy ----------
static inline void SySQueueInit(SySQueueStruct* q, std::size_t nThreads) {
    // data queue
    auto* p = new SySQNode(); // initial dummy
    q->NQPos.store(p, std::memory_order_relaxed);
    q->DQPos.store(p, std::memory_order_relaxed);

    // command nodes: allocate nThreads + 1 unique nodes
    q->AllCommands.reserve(nThreads + 1);
    for (std::size_t i = 0; i <= nThreads; ++i) q->AllCommands.push_back(new tDQCommand());

    // per-thread "next dummy": size nThreads, initial unique node each
    q->ThreadDummyCommands.resize(nThreads);
    for (std::size_t i = 0; i < nThreads; ++i) q->ThreadDummyCommands[i] = q->AllCommands[i];

    // sentinel/head for the command ring is the last unique node
    tDQCommand* sentinel = q->AllCommands.back();
    q->DQCmdPos.store(sentinel, std::memory_order_relaxed);
    q->NQCmdPos.store(sentinel, std::memory_order_relaxed);
    sentinel->Next.store(sentinel, std::memory_order_relaxed);

    q->NeedDQProxy.store(false, std::memory_order_relaxed);
}

static inline void SySQueueStartProxy(SySQueueStruct* q) {
    q->NeedDQProxy.store(true, std::memory_order_release);
    q->proxy_thread = std::thread([q](){
        while (q->NeedDQProxy.load(std::memory_order_acquire)) {
            tDQCommand* head = q->DQCmdPos.load(std::memory_order_acquire);
            tDQCommand* next = head->Next.load(std::memory_order_acquire);

            if (next == head) { // no command
                std::this_thread::yield();
                continue;
            }

            // pop command (head is current command)
            tDQCommand* current = head;
            q->DQCmdPos.store(next, std::memory_order_release);

            // serve: dequeue one node from data queue
            SySQNode* oldHead;
            SySQNode* dnxt;
            bool got = false;
            ArgVal out = EMPTY_QUEUE;
            uint64_t deq_ts = 0, in_ts = 0, call_ts = 0;

            while (true) {
                oldHead = q->DQPos.load(std::memory_order_acquire);
                dnxt    = oldHead->next.load(std::memory_order_acquire);

                if (dnxt == oldHead) { got = false; break; } // empty

                if (q->DQPos.compare_exchange_weak(oldHead, dnxt,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
                    out     = oldHead->val;
                    deq_ts  = now();
                    call_ts = oldHead->call_ts;
                    in_ts   = oldHead->in_ts;
                    got     = true;

                    oldHead->tid    = current->pid;
                    oldHead->deq_ts = deq_ts;
                    log_append(call_ts, in_ts, deq_ts);

                    delete oldHead; // safe: only proxy deletes data nodes
                    break;
                }
            }

            // respond
            current->out_val   = got ? out : EMPTY_QUEUE;
            current->had_value = got;
            current->ready.store(true, std::memory_order_release);
        }
    });
}

static inline void SySQueueStopProxy(SySQueueStruct* q) {
    q->NeedDQProxy.store(false, std::memory_order_release);
    if (q->proxy_thread.joinable()) q->proxy_thread.join();
}

// ---------- enqueue ----------
static inline void SySQueueEnqueue(SySQueueStruct* q, ArgVal arg) {
    auto* p = new SySQNode();                 // fresh empty node (next==self)
    uint64_t call_ts = now();

    std::atomic_thread_fence(std::memory_order_seq_cst);
    SySQNode* oldTail = q->NQPos.exchange(p, std::memory_order_acq_rel);

    oldTail->val = arg;
    std::atomic_thread_fence(std::memory_order_release);
    oldTail->next.store(p, std::memory_order_release);

    uint64_t in_ts = now();
    oldTail->call_ts = call_ts;
    oldTail->in_ts   = in_ts;

    q->enqueue_counter.fetch_add(1, std::memory_order_relaxed);
}

// ---------- dequeue via proxy ----------
static inline bool SySQueueDequeueProxy(SySQueueStruct* q, std::size_t tid, ArgVal& out) {
    tDQCommand* NewDummyTail = q->ThreadDummyCommands[tid];
    NewDummyTail->Next.store(NewDummyTail, std::memory_order_relaxed);
    NewDummyTail->pid = static_cast<int>(tid);
    NewDummyTail->ready.store(false, std::memory_order_relaxed);
    NewDummyTail->had_value = false;

    // publish command by swapping tail
    tDQCommand* Current = q->NQCmdPos.exchange(NewDummyTail, std::memory_order_acq_rel);
    Current->Next.store(NewDummyTail, std::memory_order_release);

    // rotate this thread's "next dummy" to the old tail (which is the command node)
    q->ThreadDummyCommands[tid] = Current;

    // wait for proxy response
    while (!Current->ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    if (!Current->had_value) return false;
    out = Current->out_val;
    q->dequeue_counter.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// ---------- destroy ----------
static inline void SySQueueDestroy(SySQueueStruct* q) {
    // 1) stop proxy
    SySQueueStopProxy(q);

    // 2) free ALL command nodes exactly once
    for (tDQCommand* cmd : q->AllCommands) delete cmd;
    q->AllCommands.clear();
    q->ThreadDummyCommands.clear();
    q->DQCmdPos.store(nullptr, std::memory_order_relaxed);
    q->NQCmdPos.store(nullptr, std::memory_order_relaxed);

    // 3) walk data queue and free remaining nodes (typically the final dummy)
    SySQNode* start = q->DQPos.load(std::memory_order_acquire);
    if (start) {
        SySQNode* next = start->next.load(std::memory_order_relaxed);
        if (next == start) {
            delete start;
        } else {
            start->next.store(nullptr, std::memory_order_relaxed); // break cycle
            SySQNode* cur = start;
            while (cur) {
                SySQNode* tmp = cur->next.load(std::memory_order_relaxed);
                delete cur;
                cur = tmp;
            }
        }
    }
    q->DQPos.store(nullptr, std::memory_order_relaxed);
    q->NQPos.store(nullptr, std::memory_order_relaxed);
}
