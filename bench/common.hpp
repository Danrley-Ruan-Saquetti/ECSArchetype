#pragma once

struct float3 {
    float x, y, z;
};
struct Position {
    float3 v;
};
struct Velocity {
    float3 v;
};

inline constexpr float kDt = 1.0f / 60.0f;

#include <cstdint>

// Mesmos bytes que T, mas com move-constructor e destrutor definidos pelo usuário:
// força o caminho não trivial (moveConstruct + destroy) em vez do memcpy.
template <class T>
struct NonTrivial {
    T v;
    NonTrivial(T x) noexcept : v(x) {}
    NonTrivial(NonTrivial&& o) noexcept : v(o.v) {}
    NonTrivial& operator=(NonTrivial&& o) noexcept {
        v = o.v;
        return *this;
    }
    ~NonTrivial() {}
};

template <int I>
struct Comp {
    uint64_t v;
};
