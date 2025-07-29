#pragma once

template <typename T>
class IQueue {
public:
  virtual void enqueue(const T& item);
  virtual bool dequeue(T& item);

}

  
