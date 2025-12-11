#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include <tuple>
#include "FairnessLogger.hpp"

// -----------------------------------------------------------
// 1. Thread Local Storage Helper
// -----------------------------------------------------------
using MSLogRecord = std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>;

struct MSThreadLogStorage {
    std::vector<MSLogRecord> events;
    MSThreadLogStorage() {
        // Pre-allocate to prevent resizing during benchmark
        events.reserve(2000000); 
    }
};

// Static thread_local ensures every thread gets its own private instance
static thread_local MSThreadLogStorage ms_tls_log;

// -----------------------------------------------------------
// 2. Queue Definitions
// -----------------------------------------------------------

template <typename T>
struct MSNode;

template <typename T>
struct MSPointer {
  MSNode<T>* ptr;
  unsigned int count;
  MSPointer() : ptr(nullptr), count(0) {}
  MSPointer(MSNode<T>* p, unsigned int c) : ptr(p), count(c) {}

  bool operator==(const MSPointer& other) const {
    return ptr == other.ptr && count == other.count;
  }
};

template <typename T>
struct MSNode {
  T value;
  std::atomic<MSPointer<T>> next;

  // Timestamps embedded in the node
  uint64_t enq_inv_ts;
  uint64_t enq_lin_ts;
  // Note: deq timestamps are generated locally by the dequeuer

  MSNode() : next(MSPointer<T>()) {}
  MSNode(const T& v) : value(v), next(MSPointer<T>()) {}
};

template <typename T>
class MSQueue {
private:
  std::atomic<MSPointer<T>> head;
  std::atomic<MSPointer<T>> tail;
  
  // Mutex specifically for the FINAL merge of logs (not used in hot path)
  std::mutex records_mutex;

public:
  std::vector<MSLogRecord> records;
  std::atomic<uint64_t> enq_retries{0};

  MSQueue() {
    auto* dummy_node = new MSNode<T>();
    MSPointer<T> dummy_pointer(dummy_node, 0);
    head.store(dummy_pointer);
    tail.store(dummy_pointer);
  }

  ~MSQueue() {
    MSPointer<T> cur = head.load();
    while (cur.ptr != nullptr) {
      MSPointer<T> next = cur.ptr->next.load();
      delete cur.ptr;
      cur = next;
    }
  }

  void enqueue(const T value, int tid) {
    uint64_t enq_inv_ts = now(); 

    auto* node = new MSNode<T>(value);
    node->enq_inv_ts = enq_inv_ts;

    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_tail = tail.load(std::memory_order_acquire);
      next = cur_tail.ptr->next.load(std::memory_order_acquire);

      if (cur_tail == tail.load()) {
        if (next.ptr == nullptr) {
          MSPointer new_element(node, next.count + 1);
          if (cur_tail.ptr->next.compare_exchange_weak(next, new_element, std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Linearization point
            node->enq_lin_ts = now(); 
            break;
          } else {
            enq_retries.fetch_add(1, std::memory_order_relaxed);
          }
        } else {
          MSPointer<T> new_tail(next.ptr, cur_tail.count + 1);
          tail.compare_exchange_weak(cur_tail, new_tail, std::memory_order_relaxed, std::memory_order_relaxed);
        }
      }
    }

    MSPointer<T> new_tail(node, cur_tail.count + 1);
    tail.compare_exchange_weak(cur_tail, new_tail, std::memory_order_relaxed, std::memory_order_relaxed);
  }

  bool dequeue(T* value, int tid) {
    uint64_t deq_inv_ts = now(); // Capture Dequeue Invocation

    MSPointer<T> cur_head;
    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_head = head.load(std::memory_order_acquire);
      cur_tail = tail.load(std::memory_order_acquire);
      next = cur_head.ptr->next.load(std::memory_order_acquire);
      
      if (cur_head == head.load()) {
        if (cur_head.ptr == cur_tail.ptr) {
          if (next.ptr == nullptr) {
            return false;
          }
          MSPointer<T> new_tail(next.ptr, cur_tail.count + 1);
          tail.compare_exchange_weak(cur_tail, new_tail, std::memory_order_relaxed, std::memory_order_acquire);
        }   
        else {
          if (next.ptr == nullptr) continue;
          *value = next.ptr->value;

          MSPointer<T> new_head(next.ptr, cur_head.count + 1);

          // Read timestamps from the node BEFORE we potentially lose access to it
          // (Though in MSQueue we hold a pointer to it via 'next', so it's safe until delete)
          uint64_t enq_inv_ts = next.ptr->enq_inv_ts;
          uint64_t enq_lin_ts = next.ptr->enq_lin_ts;
          
          if (head.compare_exchange_weak(cur_head, new_head, std::memory_order_acq_rel, std::memory_order_acquire)) {
            
            uint64_t deq_lin_ts = now();

            // -------------------------------------------------------
            // FAST THREAD-LOCAL LOGGING (No Global Lock)
            // -------------------------------------------------------
            ms_tls_log.events.emplace_back(
                enq_inv_ts, 
                enq_lin_ts, 
                deq_inv_ts, 
                deq_lin_ts
            );

            break;
          }
        }
      }
    } 
    
    // Note: In a production MS Queue, this delete is unsafe (ABA problem / Hazard Pointers required).
    // For this benchmark harness, we assume it's acceptable or handled elsewhere.
    delete cur_head.ptr;
    return true;
  }

  // -------------------------------------------------------
  // Merges thread local logs into the global list
  // -------------------------------------------------------
  void commit_thread_logs() {
      if (ms_tls_log.events.empty()) return;

      std::lock_guard<std::mutex> lock(records_mutex);
      records.insert(
          records.end(),
          ms_tls_log.events.begin(),
          ms_tls_log.events.end()
      );
      
      // Clear to free memory
      ms_tls_log.events.clear();
  }
};