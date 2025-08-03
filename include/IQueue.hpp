#pragma once

#include "FairnessLogger.hpp"
#include "IClock.hpp"

template <typename T>
class IQueue {
protected:
  FairnessLogger& logger;
  IClock& clock;
  
public:
  IQueue(FairnessLogger& logger_, IClock& clock_)
    : logger(logger_), clock(clock_) {}
  
  virtual void enqueue(const T value, int tid) = 0;
  virtual bool dequeue(T* value, int tid) = 0;
  virtual ~IQueue() = default;
};

  
