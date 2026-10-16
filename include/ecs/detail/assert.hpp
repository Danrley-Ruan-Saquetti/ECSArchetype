#pragma once

#ifndef ECS_ENABLE_ASSERTS
#ifdef NDEBUG
#define ECS_ENABLE_ASSERTS 0
#else
#define ECS_ENABLE_ASSERTS 1
#endif
#endif

namespace ecs {

  using AssertHandler = void (*)(const char* message) noexcept;

  // Instala o handler e devolve o anterior. Padrão: escreve em stderr e chama std::abort().
  AssertHandler setAssertHandler(AssertHandler handler) noexcept;

  namespace detail {
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 4, 5)))
#endif
    [[noreturn]] void assertFail(const char* expr, const char* file, int line, const char* fmt, ...) noexcept;
  } // namespace detail

} // namespace ecs

#if ECS_ENABLE_ASSERTS
#define ECS_ASSERT(cond, ...)                                            \
  do {                                                                   \
    if (!(cond))                                                         \
      ::ecs::detail::assertFail(#cond, __FILE__, __LINE__, __VA_ARGS__); \
  } while (0)
#else
#define ECS_ASSERT(cond, ...) \
  do {                        \
  } while (0)
#endif
