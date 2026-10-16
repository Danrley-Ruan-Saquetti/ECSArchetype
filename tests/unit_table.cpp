#include <doctest/doctest.h>
#include <ecs/detail/assert.hpp>

TEST_CASE("smoke: asserts ligados em debug") {
#ifdef NDEBUG
    CHECK(ECS_ENABLE_ASSERTS == 0);
#else
    CHECK(ECS_ENABLE_ASSERTS == 1);
#endif
    ECS_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}
