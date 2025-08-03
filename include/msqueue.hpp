#include <atomic>

#include "FairnessMetrics.hpp"
#include "IQueue.hpp"
#include "InstrumentedNode.hpp"
#include "FairnessLogger.hpp"
#include "IClock.hpp"

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
struct MSNode : public InstrumentedNode {
  T value;
  std::atomic<MSPointer<T>> next;
  MSNode() : next(MSPointer<T>()) {}
  MSNode(const T& v) : value(v), next(MSPointer<T>()) {}
};

template <typename T>
class MSQueue : public IQueue<T> {
private:
  std::atomic<MSPointer<T>> head;
  std::atomic<MSPointer<T>> tail;

public:
  std::vector<std::tuple<int,double,double,double>> records;
  MSQueue(FairnessLogger& lg, IClock& cl) : IQueue<T>(lg, cl) {
    auto* dummy_node = new MSNode<T>();
    MSPointer<T> dummy_pointer(dummy_node, 0);
    head.store(dummy_pointer);
    tail.store(dummy_pointer);
    records.reserve(12000);
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
    double call_ts = this->clock.now();
    auto* node = new MSNode<T>(value);

    //if (this->logger.is_enabled(FairnessMetric::ENQUEUE_CALL)) {
  
    node->call_ts = call_ts;
    //node->call_ts = std::chrono::duration<double>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
      // }

    node->tid = tid;

    MSPointer<T> cur_tail;
    MSPointer<T> next;

    while (true) {
      cur_tail = tail.load();
      next = cur_tail.ptr->next.load();

      if (cur_tail == tail.load()) {
	if (next.ptr == nullptr) {
	  MSPointer new_element(node, next.count + 1);
	  if (cur_tail.ptr->next.compare_exchange_weak(next, new_element)) {
	    // if (this->logger.is_enabled(FairnessMetric::ENQUEUE_IN)) {
	    double in_ts = this->clock.now();
	    node->in_ts = in_ts;
	      //   }
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
    // this->logger.log_enqueue(node->tid, node->call_ts, node->in_ts);
  }

  bool dequeue(T* value, int tid) {
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

	  //  if (this->logger.is_enabled(FairnessMetric::DEQUEUE)) {
	  next.ptr->deq_ts = this->clock.now();
	  
	  std::lock_guard<std::mutex> lock(mtx);
	  records.emplace_back(next.ptr->tid, next.ptr->call_ts, next.ptr->in_ts, next.ptr->deq_ts);
	  
	    //no,this->logger.log_dequeue(next.ptr->tid, next.ptr->call_ts, next.ptr->in_ts, next.ptr->deq_ts);
	    // we wait until the dequeue to log everything. enqueue just records the data to the node
	    //  }
	  
	  MSPointer<T> new_head(next.ptr, cur_head.count + 1);
	  if (head.compare_exchange_strong(cur_head, new_head)) break;
	}
      }
    }
    
    delete cur_head.ptr;
    return true;
  }
};
