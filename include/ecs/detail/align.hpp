#pragma once

#include <cstddef>
#include <new>

namespace ecs {
  constexpr size_t alignUp(size_t v, size_t a) noexcept {
    return (v + a - 1) & ~(a - 1);
  }

  inline std::byte* alignedAlloc(size_t bytes, size_t align) {
    return static_cast<std::byte*>(::operator new(bytes, std::align_val_t(align)));
  }
  inline void alignedFree(std::byte* p, size_t align) noexcept {
    ::operator delete(p, std::align_val_t(align));
  }
} // namespace ecs
