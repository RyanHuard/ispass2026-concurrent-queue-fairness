#pragma once

#include "IClock.hpp"
#include <chrono>

class HighResolutionClock : public IClock {
public:

  double now() override {
    auto t = std::chrono::high_resolution_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
    return ns;
  }

  };
