#include <atomic>

#include "IQueue.hpp"

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
  MSNode() : next(MSPointer<T>{nullptr, 0}) {}
  MSNode(const T& v) : value(v), next(MSPointer<T>{nullptr, 0}) {}
};

template <typename T>
class MSQueue : public IQueue<T> {
private:
  std::atomic<MSPointer<T>> head;
  std::atomic<MSPointer<T>> tail;

public:
  MSQueue() {
    auto* dummy_node = new MSNode<T>();
    MSPointer<T> dummy_pointer(dummy_node, 0);
    head.store(dummy_pointer);
    tail.store(dummy_pointer);
  }

  ~MSQueue() {
    T temp;
    while (dequeue(&temp));
    MSPointer<T> final = head.load();
    delete final.ptr;
  }

  void enqueue(const T value) {
    auto* node = new MSNode<T>;
    node->value = value;
    node->next.store(MSPointer<T>(nullptr, 0));

    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_tail = tail.load();
      next = cur_tail.ptr->next.load();

      if (cur_tail == tail.load()) {
	if (next.ptr == nullptr) {
	  MSPointer new_element(node, next.count + 1);
	  if (cur_tail.ptr->next.compare_exchange_weak(next, new_element)) {
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
	  MSPointer<T> new_head(next.ptr, cur_head.count + 1);
	  if (head.compare_exchange_weak(cur_head, new_head)) break;
	}
      }
    }
    delete cur_head.ptr;
    return true;
  }
};
