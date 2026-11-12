# `core`

Dependency-free utilities, used by every module (`engine/runtime/core/`):

- `assert.hpp`: `OLY_ASSERT(cond, fmt, ...)`. Compiled out with `OLY_ENABLE_ASSERTS=0`. The message includes the operation, the `Entity` and the type name. The handler is replaceable, which enables the death tests ([build-and-tests.md](build-and-tests.md#tests)).
- `align.hpp`: alignment and aligned allocation.
- `small_map.hpp`: small map backed by a sorted vector, used in the ECS edges and component index.
- `type_name.hpp`: compile-time type name, for messages.
