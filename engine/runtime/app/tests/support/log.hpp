#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "oly/app/engine.hpp"

struct Position {
  float x = 0, y = 0;
};
struct Velocity {
  float x = 0, y = 0;
};

struct Log {
  std::vector<std::string> calls;
  std::vector<float> interpolations;
  std::vector<double> times;

  int count(const std::string& name) const {
    return static_cast<int>(std::count(calls.begin(), calls.end(), name));
  }
};

inline void recordName(oly::app::SystemContext& context) {
  Log& log = *static_cast<Log*>(context.userData());
  log.calls.push_back(std::string(context.name()));

  if (context.phase() == oly::app::Phase::Render) {
    log.interpolations.push_back(context.interpolation());
  } else {
    log.times.push_back(context.time());
  }
}
