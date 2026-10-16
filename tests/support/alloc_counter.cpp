#include "alloc_counter.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
  std::atomic<size_t> g_count{0};

  void* allocate(size_t n) {
    g_count.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1))
      return p;
    throw std::bad_alloc();
  }
  void* allocateAligned(size_t n, std::align_val_t al) {
    g_count.fetch_add(1, std::memory_order_relaxed);
    const size_t a = static_cast<size_t>(al);
    n = (n + a - 1) / a * a; // aligned_alloc exige múltiplo do alinhamento
    if (void* p = std::aligned_alloc(a, n ? n : a))
      return p;
    throw std::bad_alloc();
  }
} // namespace

size_t allocationCount() noexcept {
  return g_count.load(std::memory_order_relaxed);
}

void* operator new(size_t n) {
  return allocate(n);
}
void* operator new[](size_t n) {
  return allocate(n);
}
void* operator new(size_t n, const std::nothrow_t&) noexcept {
  try {
    return allocate(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t n, const std::nothrow_t&) noexcept {
  try {
    return allocate(n);
  } catch (...) {
    return nullptr;
  }
}
void* operator new(size_t n, std::align_val_t al) {
  return allocateAligned(n, al);
}
void* operator new[](size_t n, std::align_val_t al) {
  return allocateAligned(n, al);
}
void* operator new(size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(n, al);
  } catch (...) {
    return nullptr;
  }
}
void* operator new[](size_t n, std::align_val_t al, const std::nothrow_t&) noexcept {
  try {
    return allocateAligned(n, al);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete[](void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, size_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, size_t) noexcept {
  std::free(p);
}
void operator delete(void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}
void operator delete[](void* p, const std::nothrow_t&) noexcept {
  std::free(p);
}
void operator delete(void* p, std::align_val_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::align_val_t) noexcept {
  std::free(p);
}
void operator delete(void* p, size_t, std::align_val_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, size_t, std::align_val_t) noexcept {
  std::free(p);
}
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(p);
}
