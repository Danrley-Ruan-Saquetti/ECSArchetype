#pragma once

#include <cstdint>
#include <string>

struct A {
  uint64_t value;
};
struct B {
  uint64_t first, second;
};
struct Tag {};
struct alignas(64) Wide {
  float values[16];
};

struct LifeCounter {
  static inline int64_t alive = 0;

  uint64_t value = 0;

  LifeCounter() noexcept {
    ++alive;
  }
  explicit LifeCounter(uint64_t initialValue) noexcept
    : value(initialValue) {
    ++alive;
  }
  LifeCounter(const LifeCounter& other) noexcept
    : value(other.value) {
    ++alive;
  }
  LifeCounter(LifeCounter&& other) noexcept
    : value(other.value) {
    ++alive;
  }
  LifeCounter& operator=(const LifeCounter&) noexcept = default;
  LifeCounter& operator=(LifeCounter&&) noexcept = default;
  ~LifeCounter() {
    --alive;
  }
};

// Requires doctest to be included before use.
#define CHECK_INVARIANTS(world)                          \
  do {                                                   \
    std::string why_;                                    \
    CHECK_MESSAGE(oly::ecs::checkInvariants(world, &why_), why_); \
  } while (0)
