#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
  std::atomic<size_t> g_count{0};

  void* allocate(size_t size) {
    g_count.fetch_add(1, std::memory_order_relaxed);
    if (void* pointer = std::malloc(size ? size : 1)) {
      return pointer;
    }

    throw std::bad_alloc();
  }

  void* allocateAligned(size_t size, std::align_val_t alignment) {
    g_count.fetch_add(1, std::memory_order_relaxed);

    const size_t alignmentBytes = static_cast<size_t>(alignment);
    size = (size + alignmentBytes - 1) / alignmentBytes * alignmentBytes; // aligned_alloc exige múltiplo do alinhamento
    if (void* pointer = std::aligned_alloc(alignmentBytes, size ? size : alignmentBytes)) {
      return pointer;
    }

    throw std::bad_alloc();
  }
}

size_t allocationCount() noexcept {
  return g_count.load(std::memory_order_relaxed);
}

void* operator new(size_t size) {
  return allocate(size);
}
void* operator new[](size_t size) {
  return allocate(size);
}
void* operator new(size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, const std::nothrow_t&) noexcept {
  try {
    return allocate(size);
  } catch (...) {
    return nullptr;
  }
}
void* operator new(size_t size, std::align_val_t alignment) {
  return allocateAligned(size, alignment);
}
void* operator new[](size_t size, std::align_val_t alignment) {
  return allocateAligned(size, alignment);
}
void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(size, alignment);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(size, alignment);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* pointer) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, size_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, size_t) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, size_t, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, size_t, std::align_val_t) noexcept {
  std::free(pointer);
}
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(pointer);
}
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(pointer);
}
