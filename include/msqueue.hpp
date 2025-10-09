#pragma once

#include <atomic>
#include "FairnessMetrics.hpp"
#include "FairnessLogger.hpp"


std::mutex mtx;

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

  uint64_t enq_inv_ts;
  uint64_t enq_lin_ts;
  uint64_t deq_inv_ts;
  uint64_t deq_lin_ts;

  MSNode() : next(MSPointer<T>()) {}
  MSNode(const T& v) : value(v), next(MSPointer<T>()) {}
};

template <typename T>
class MSQueue {
private:
  std::atomic<MSPointer<T>> head;
  std::atomic<MSPointer<T>> tail;

public:
  std::vector<std::tuple<uint64_t, uint64_t, uint64_t, uint64_t>> records;

  MSQueue() {
    auto* dummy_node = new MSNode<T>();
    MSPointer<T> dummy_pointer(dummy_node, 0);
    head.store(dummy_pointer);
    tail.store(dummy_pointer);
    records.reserve(165000);
  }

  ~MSQueue() {
    // Walk from head and delete every node
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
            node->enq_lin_ts = now();
	          break;
	        }
	      }
        else {
          MSPointer<T> new_tail(next.ptr, cur_tail.count + 1);
          tail.compare_exchange_weak(cur_tail, new_tail, std::memory_order_relaxed, std::memory_order_relaxed);
        }
      }
    }

    MSPointer<T> new_tail(node, cur_tail.count + 1);
    tail.compare_exchange_weak(cur_tail, new_tail, std::memory_order_relaxed, std::memory_order_relaxed);
  }

  bool dequeue(T* value, int tid) {
    uint64_t deq_inv_ts = now();

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

          uint64_t enq_inv_ts = next.ptr->enq_inv_ts;
          uint64_t enq_lin_ts = next.ptr->enq_lin_ts;
          
          if (head.compare_exchange_weak(cur_head, new_head, std::memory_order_acq_rel, std::memory_order_acquire)) {
            uint64_t deq_lin_ts = now();

            std::lock_guard<std::mutex> lock(mtx);
            records.emplace_back(enq_inv_ts, enq_lin_ts, deq_inv_ts, deq_lin_ts);
            
            break;
          }
        }
      }
    } 
    
    delete cur_head.ptr;
    return true;
  }
};
