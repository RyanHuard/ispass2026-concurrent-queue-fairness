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
    
  FairnessLogger&       get_logger()       { return logger; }
  const FairnessLogger& get_logger() const { return logger; }
  
  virtual void enqueue(const T value) = 0;
  virtual bool dequeue(T* value) = 0;
  virtual ~IQueue() = default;
};

  
