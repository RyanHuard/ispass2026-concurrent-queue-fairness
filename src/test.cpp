// #include <atomic>
// #include <cstddef>
// #include <thread>
// #include <vector>
// #include <iostream>
// #include <tuple>
// #include <mutex>


// #ifndef __CACHEALIGN__
// #define __CACHEALIGN__ alignas(64)
// #endif

// #include <immintrin.h> 
// #include <x86intrin.h>
// static inline uint64_t now() {
//     unsigned aux;
//     uint64_t ts = __rdtscp(&aux);  
//     _mm_lfence(); 
//     return ts;
// }


// template <typename T>
// struct tNode {
//     explicit tNode(T* _Payload = nullptr)
//         : Next(this), Payload(_Payload), NeedProxy(false) {}

//     // modernized: use atomics instead of volatile
//     std::atomic<tNode*> Next;       // points to self when “isolated”
//     std::atomic<T*>     Payload;    // becomes non-null when published
//     bool NeedProxy;
//     uint64_t in_ts{0}, call_ts{0}, deq_ts{0};
// };

// template <typename T>
// struct tQueue {
//     tQueue() : DQPos(nullptr), NQPos(nullptr),
//                DQCmdPos(nullptr), NQCmdPos(nullptr),
//                NoDataYet(nullptr), NeedProxy(false),
//                DummyNode(nullptr) {}

//     virtual ~tQueue() = default;
//     std::mutex mtx;
//     std::vector<std::tuple<uint64_t, uint64_t, uint64_t>> records;
//     struct tDQCmd {
//         std::atomic<tDQCmd*> Next;
//         std::atomic<tNode<T>*> Response;
//     } __CACHEALIGN__;

//     void Open(std::size_t MaxThreads) {
//         DummyNode = new tNode<T>();
//         NQPos.store(DummyNode, std::memory_order_relaxed);
//         DQPos.store(DummyNode, std::memory_order_relaxed);

//         // per-thread command “dummies”
//         DummyCommands.resize(MaxThreads);
//         for (auto& cmdPtr : DummyCommands) {
//             cmdPtr = new tDQCmd();
//             cmdPtr->Next.store(cmdPtr, std::memory_order_relaxed);
//             cmdPtr->Response.store(nullptr, std::memory_order_relaxed);
//         }

//         // sentinel for command list
//         tDQCmd* sentinel = new tDQCmd();
//         sentinel->Next.store(sentinel, std::memory_order_relaxed);
//         sentinel->Response.store(nullptr, std::memory_order_relaxed);
//         DQCmdPos.store(sentinel, std::memory_order_relaxed);
//         NQCmdPos.store(sentinel, std::memory_order_relaxed);

//         NoDataYet = new tNode<T>(); // special marker

//         NeedProxy.store(true, std::memory_order_release);
//         ProxyThread = std::thread([this] { DequeueProxyThread(); });
//     }

//     void Close() {
//         NeedProxy.store(false, std::memory_order_release);
//         if (ProxyThread.joinable()) ProxyThread.join();

//         // best-effort cleanup
//         auto dq = DQPos.load(std::memory_order_acquire);
//         if (dq) dq->Next.store(nullptr, std::memory_order_release);

//         DQPos.store(nullptr, std::memory_order_release);
//         NQPos.store(nullptr, std::memory_order_release);

//         auto dqc = DQCmdPos.load(std::memory_order_acquire);
//         if (dqc) dqc->Next.store(nullptr, std::memory_order_release);
//         DQCmdPos.store(nullptr, std::memory_order_release);
//         NQCmdPos.store(nullptr, std::memory_order_release);

//         for (auto* p : DummyCommands) delete p;
//         DummyCommands.clear();

//         delete DummyNode;  DummyNode = nullptr;
//         delete NoDataYet;  NoDataYet = nullptr;
//     }

//     void enqueue(const T& v, std::size_t) {
//         auto* p = new T(v);
//         auto* n = new tNode<T>(p);
//         Enqueue(n);
//     }

//     bool dequeue(T& out, std::size_t tid) {
//         tNode<T>* n = Dequeue(tid);
//         if (!n) return false;                 // queue empty (for now)

//         T* p = n->Payload.load(std::memory_order_acquire);
//         if (!p) { delete n; return false; }   // defensive
//         out = *p;
//         delete p;
//         delete n;
//         return true;
//     }

//     void Enqueue(tNode<T>* Node) {
//         // detach node and publish payload via predecessor
//         T* payload = Node->Payload.exchange(nullptr, std::memory_order_relaxed);
//         Node->Next.store(Node, std::memory_order_relaxed);

//         tNode<T>* pred = NQPos.exchange(Node, std::memory_order_release);

//         pred->Payload.store(payload, std::memory_order_release);
//         std::atomic_thread_fence(std::memory_order_release);
//         pred->Next.store(Node, std::memory_order_release);
//         std::atomic_thread_fence(std::memory_order_release);
//     }

//     tNode<T>* Dequeue(std::size_t TID) {
//         tDQCmd* newDummyTail = DummyCommands[TID];
//         newDummyTail->Next.store(newDummyTail, std::memory_order_relaxed);

//         tDQCmd* current =
//             NQCmdPos.exchange(newDummyTail, std::memory_order_acq_rel);
//         DummyCommands[TID] = current;

//         current->Response.store(NoDataYet, std::memory_order_relaxed);
//         current->Next.store(newDummyTail, std::memory_order_release);

//         // spin until proxy serves this command
//         for (;;) {
//             std::atomic_thread_fence(std::memory_order_acquire);
//             tNode<T>* r = current->Response.load(std::memory_order_acquire);
//             if (r != NoDataYet) return r; // nullptr means “queue empty right now”
//             std::this_thread::yield();
//         }
//     }

// private:
//     void DequeueProxyThread() {
//         for (;;) {
//             std::atomic_thread_fence(std::memory_order_acquire);
//             if (!NeedProxy.load(std::memory_order_acquire)) break;

//             // any pending command?
//             tDQCmd* head = DQCmdPos.load(std::memory_order_acquire);
//             tDQCmd* next = head->Next.load(std::memory_order_acquire);
//             if (next == head) {
//                 std::this_thread::yield();
//                 continue;
//             }

//             // pop one command
//             DQCmdPos.store(next, std::memory_order_release);
//             tDQCmd* current = head;

//             // serve one dequeue
//             for (;;) {
//                 tNode<T>* dqHead = DQPos.load(std::memory_order_acquire);
//                 tNode<T>* dnxt   = dqHead->Next.load(std::memory_order_acquire);

//                 if (dnxt == dqHead) {
//                     // queue appears empty (no published next)
//                     tNode<T>* nqTail = NQPos.load(std::memory_order_acquire);
//                     if (dqHead == nqTail) {
//                         current->Response.store(nullptr, std::memory_order_release);
//                         break;
//                     } else {
//                         // producers racing; retry
//                         std::this_thread::yield();
//                         continue;
//                     }
//                 } else {
//                     // pop head
                    
//                     current->Response.store(dqHead, std::memory_order_relaxed);
//                     DQPos.store(dnxt, std::memory_order_release);
//                     {
//                     std::lock_guard<std::mutex> lock(mtx);
//                     records.emplace_back(now(), now(), now());
//                     }
//                     break;
//                 }
//             }

//             std::atomic_thread_fence(std::memory_order_release);
//         }
//     }

// private:
//     // queue state
//     std::atomic<tNode<T>*> DQPos __CACHEALIGN__;      // head-like pointer
//     std::atomic<tNode<T>*> NQPos __CACHEALIGN__;      // tail-like pointer

//     // command queue for dequeuers
//     std::atomic<tDQCmd*>   DQCmdPos __CACHEALIGN__;   // “head”
//     std::atomic<tDQCmd*>   NQCmdPos __CACHEALIGN__;   // “tail”
//     std::vector<tDQCmd*>   DummyCommands __CACHEALIGN__;

//     tNode<T>*              NoDataYet;
//     std::thread            ProxyThread;
//     std::atomic<bool>      NeedProxy;
//     tNode<T>*              DummyNode;
// };

// #include <algorithm>


// struct OvertakeDepthStats {
//     double mean_all_elements = 0.0;   // avg overtake depth among all items
//     double mean_overtaken_elements = 0.0; // avg overtake depth among items that got overtaken
//     size_t max_depth = 0;// max depth of an overtake
//     size_t count = 0;    // # items with depth > 0
//     double pct = 0.0;    // 100 * count_overtaken / n
// };

// struct Fenwick {
//     std::vector<int> bit;
//     explicit Fenwick(int n) : bit(n + 1, 0) {}
//     void add(int idx, int val) { for (; idx < (int)bit.size(); idx += idx & -idx) bit[idx] += val; }
//     int sum_prefix(int idx) const { int s = 0; for (; idx > 0; idx -= idx & -idx) s += bit[idx]; return s; }
// };


// OvertakeDepthStats compute_overtake_metrics(
//     const std::vector<std::tuple<uint64_t, uint64_t, uint64_t>>& records)
// {
//     OvertakeDepthStats res;
//     if (records.empty()) return res;

//     // Build (e1,e2), skipping e1==0 (e.g., in_ts==0)
//     std::vector<std::pair<uint64_t,uint64_t>> ts_pairs;
//     ts_pairs.reserve(records.size());
//     for (const auto& rec : records) {
//         const uint64_t e1 = 0;
//         if (e1 == 0) continue;                 // <-- skip zero in_ts
//         const uint64_t e2 = 1;
//         ts_pairs.emplace_back(e1, e2);
//     }
//     if (ts_pairs.empty()) return res;

//     // Sort by event1 asc, then event2 asc (tie-break doesn’t affect correctness)
//     std::stable_sort(ts_pairs.begin(), ts_pairs.end(),
//         [](const auto& a, const auto& b){
//             if (a.first != b.first) return a.first < b.first;
//             return a.second < b.second;
//         });

//     // Coordinate-compress event2 to [1..M]
//     std::vector<uint64_t> e2_vals;
//     e2_vals.reserve(ts_pairs.size());
//     for (auto& p : ts_pairs) e2_vals.push_back(p.second);
//     std::sort(e2_vals.begin(), e2_vals.end());
//     e2_vals.erase(std::unique(e2_vals.begin(), e2_vals.end()), e2_vals.end());
//     auto rank_e2 = [&](uint64_t v){
//         return (int)(std::lower_bound(e2_vals.begin(), e2_vals.end(), v) - e2_vals.begin()) + 1; // 1-based
//     };

//     // Sweep from largest event1 to smallest.
//     // Process equal-e1 items as a group: query first, then insert all from the group.
//     const int M = (int)e2_vals.size();
//     Fenwick bit(M);

//     std::vector<std::size_t> depth(ts_pairs.size(), 0);

//     std::size_t i = ts_pairs.size();
//     while (i > 0) {
//         std::size_t j = i;
//         const uint64_t e1 = ts_pairs[i-1].first;
//         // group is [k..i-1] with same e1
//         while (j > 0 && ts_pairs[j-1].first == e1) --j;

//         // Query depths for this group (tree has only strictly larger event1s)
//         for (std::size_t k = j; k < i; ++k) {
//             int r = rank_e2(ts_pairs[k].second);
//             // strict < on event2 ⇒ use prefix up to r-1
//             depth[k] = (r > 1) ? bit.sum_prefix(r - 1) : 0;
//         }

//         // Insert this group's e2 into the tree
//         for (std::size_t k = j; k < i; ++k) {
//             int r = rank_e2(ts_pairs[k].second);
//             bit.add(r, 1);
//         }

//         i = j;
//     }

//     // Aggregate stats
//     uint64_t sum = 0;
//     for (auto d : depth) {
//         sum += d;
//         if (d > res.max_depth) res.max_depth = d;
//         if (d > 0) ++res.count;
//     }

//     const double n = (double)depth.size();
//     res.mean_all_elements = (n > 0) ? (double)sum / n : 0.0;
//     res.mean_overtaken_elements = (res.count > 0) ? (double)sum / (double)res.count : 0.0;
//     res.pct = (n > 0) ? 100.0 * (double)res.count / n : 0.0;

//     return res;
// }




// int main() {
//     using T = int;
//     tQueue<T> q;

//     constexpr std::size_t kThreads = 4;
//     constexpr int num_ops = 100000;
//     q.Open(kThreads);

//     std::vector<std::thread> threads;
//     for (std::size_t tid = 0; tid < kThreads; ++tid) {
//         threads.emplace_back([&, tid] {
//             T value;
//             for (int i = 0; i < num_ops; i++) {
//                 q.enqueue(i, tid);
//                 while (!q.dequeue(value, tid)) {
//                     std::this_thread::yield();
//                 }
               
//             }
//         });
//     }

//     for (auto& t : threads) t.join();
//     auto& records = q.records;
//     auto ins = compute_overtake_metrics(records);
//         size_t in_ts_zero = 0, deq_before_in = 0;
//     for (auto& rec : q.records) {
//         uint64_t call_ts = std::get<0>(rec);
//         uint64_t in_ts   = std::get<1>(rec);
//         uint64_t deq_ts  = std::get<2>(rec);
//         if (in_ts == 0) ++in_ts_zero;
//         if (deq_ts < in_ts) ++deq_before_in;
//     }
//     std::cerr << "in_ts_zero=" << in_ts_zero
//               << " deq_before_in=" << deq_before_in
//               << " records=" << q.records.size() << "\n";

          
//     q.Close();
//     return 0;
// }
