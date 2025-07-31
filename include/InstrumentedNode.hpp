#pragma once

struct InstrumentedNode {
  int tid{-1};
  double call_ts{0.0};
  double in_ts{0.0};
  double deq_ts{0.0};
};
