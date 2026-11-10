#pragma once

#include <cstddef>
#include <new>

namespace ecs {
  constexpr size_t alignUp(size_t value, size_t alignment) noexcept {
    return (value + alignment - 1) & ~(alignment - 1);
  }

  inline std::byte* alignedAllocate(size_t bytes, size_t alignment) {
    return static_cast<std::byte*>(::operator new(bytes, std::align_val_t(alignment)));
  }
  inline void alignedFree(std::byte* pointer, size_t alignment) noexcept {
    ::operator delete(pointer, std::align_val_t(alignment));
  }
}
