#pragma once

#include "IClock.hpp"
#include <chrono>

class HighResolutionClock : public IClock {
public:
   inline double now() override {
    return std::chrono::high_resolution_clock::now()
             .time_since_epoch()
             .count();
  }

  };
