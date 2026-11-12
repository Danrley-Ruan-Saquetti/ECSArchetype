#pragma once

#include <cstdint>

struct float3 {
  float x, y, z;
};
struct Position {
  float3 value;
};
struct Velocity {
  float3 value;
};

inline constexpr float kDeltaTime = 1.0f / 60.0f;

// Same bytes as T, but with a user-defined move constructor and destructor:
// forces the non-trivial path (moveConstruct + destroy) instead of memcpy.
template <class T>
struct NonTrivial {
  T value;

  NonTrivial(T initialValue) noexcept
    : value(initialValue) {}
  NonTrivial(NonTrivial&& other) noexcept
    : value(other.value) {}
  NonTrivial& operator=(NonTrivial&& other) noexcept {
    value = other.value;
    return *this;
  }
  ~NonTrivial() {}
};

template <int I>
struct Component {
  uint64_t value;
};
