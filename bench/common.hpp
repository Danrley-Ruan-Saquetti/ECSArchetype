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
