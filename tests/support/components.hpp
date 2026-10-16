#pragma once
#include <cstdint>

struct A {
    uint64_t v;
};
struct B {
    uint64_t v, w;
};
struct Tag {};
struct alignas(64) Wide {
    float f[16];
};

struct LifeCounter {
    static inline int64_t alive = 0;
    uint64_t v = 0;
    LifeCounter() noexcept { ++alive; }
    explicit LifeCounter(uint64_t x) noexcept : v(x) { ++alive; }
    LifeCounter(const LifeCounter& o) noexcept : v(o.v) { ++alive; }
    LifeCounter(LifeCounter&& o) noexcept : v(o.v) { ++alive; }
    LifeCounter& operator=(const LifeCounter&) noexcept = default;
    LifeCounter& operator=(LifeCounter&&) noexcept = default;
    ~LifeCounter() { --alive; }
};
