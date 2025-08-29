#pragma once

#include <atomic>
#include "FairnessMetrics.hpp"
#include "FairnessLogger.hpp"
#include "IClock.hpp"
#include "IQueue.hpp"

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
  double call_ts;
  double in_ts;
  double deq_ts;
  MSNode() : next(MSPointer<T>()) {}
  MSNode(const T& v) : value(v), next(MSPointer<T>()) {}
};

template <typename T>
class MSQueue {
private:
  std::atomic<MSPointer<T>> head;
  std::atomic<MSPointer<T>> tail;

public:
  std::vector<std::tuple<double,double,double>> records;
  MSQueue() {
    auto* dummy_node = new MSNode<T>();
    MSPointer<T> dummy_pointer(dummy_node, 0);
    head.store(dummy_pointer);
    tail.store(dummy_pointer);
    records.reserve(25000);
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

  void enqueue(const T value) {
    double call_ts = now();
    auto* node = new MSNode<T>(value);
  
    node->call_ts = call_ts;

    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_tail = tail.load();
      next = cur_tail.ptr->next.load();

      if (cur_tail == tail.load()) {
	      if (next.ptr == nullptr) {
	        MSPointer new_element(node, next.count + 1);
	        if (cur_tail.ptr->next.compare_exchange_weak(next, new_element)) {
            double in_ts = now();
            node->in_ts = in_ts;
	          break;
	        }
	      }
        else {
          MSPointer<T> new_tail(next.ptr, cur_tail.count + 1);
          tail.compare_exchange_weak(cur_tail, new_tail);
        }
      }
    }

    MSPointer<T> new_tail(node, cur_tail.count + 1);
    tail.compare_exchange_weak(cur_tail, new_tail);
  }

  bool dequeue(T* value) {
    MSPointer<T> cur_head;
    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_head = head.load();
      cur_tail = tail.load();
      next = cur_head.ptr->next.load();

      if (cur_head == head.load()) {
        if (cur_head.ptr == cur_tail.ptr) {
          if (next.ptr == nullptr) {
            return false;
          }
          MSPointer<T> new_tail(next.ptr, cur_tail.count + 1);
          tail.compare_exchange_weak(cur_tail, new_tail);
	      }   
        else {
          if (next.ptr == nullptr) continue;
          *value = next.ptr->value;

          double deq_ts = now();
          next.ptr->deq_ts = deq_ts;
          
          std::lock_guard<std::mutex> lock(mtx);
          records.emplace_back(next.ptr->call_ts, next.ptr->in_ts, next.ptr->deq_ts);
          
          MSPointer<T> new_head(next.ptr, cur_head.count + 1);
          if (head.compare_exchange_strong(cur_head, new_head)) break;
        }
      }
    } 
    
    delete cur_head.ptr;
    return true;
  }
};
