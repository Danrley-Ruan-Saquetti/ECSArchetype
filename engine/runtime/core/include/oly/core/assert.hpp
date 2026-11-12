#pragma once

#ifndef OLY_ENABLE_ASSERTS
#ifdef NDEBUG
#define OLY_ENABLE_ASSERTS 0
#else
#define OLY_ENABLE_ASSERTS 1
#endif
#endif

namespace oly::core {
  using AssertHandler = void (*)(const char* message) noexcept;

  // Installs the handler and returns the previous one; nullptr restores the default (writes to stderr and calls std::abort()).
  // If the handler returns, the process aborts anyway.
  AssertHandler setAssertHandler(AssertHandler handler) noexcept;

  namespace detail {
    [[noreturn]]
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    void assertFail(const char* expression, const char* file, int line, const char* format, ...) noexcept;
  }
}

#if OLY_ENABLE_ASSERTS
#define OLY_ASSERT(condition, ...)                                                  \
  do {                                                                              \
    if (!(condition)) {                                                             \
      ::oly::core::detail::assertFail(#condition, __FILE__, __LINE__, __VA_ARGS__); \
    }                                                                               \
  } while (0)
#else
#define OLY_ASSERT(condition, ...) \
  do {                             \
  } while (0)
#endif
