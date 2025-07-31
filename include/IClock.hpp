#pragma once

class IClock {
public:
  virtual double now() = 0;
  virtual ~IClock() = default;
  };
