#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ecs/detail/assert.hpp>

namespace ecs {
  namespace {
    void defaultHandler(const char* message) noexcept {
      std::fprintf(stderr, "%s\n", message);
      std::fflush(stderr);
      std::abort();
    }
    AssertHandler g_handler = &defaultHandler;
  } // namespace

  AssertHandler setAssertHandler(AssertHandler handler) noexcept {
    AssertHandler previous = g_handler;
    g_handler = handler ? handler : &defaultHandler;
    return previous;
  }

  namespace detail {
    void assertFail(const char* expr, const char* file, int line, const char* fmt, ...) noexcept {
      char msg[256];
      va_list args;
      va_start(args, fmt);
      std::vsnprintf(msg, sizeof msg, fmt, args);
      va_end(args);
      char full[512];
      std::snprintf(full, sizeof full, "ECS_ASSERT failed: %s | %s (%s:%d)", expr, msg, file, line);
      g_handler(full);
      std::abort();
    }
  } // namespace detail
} // namespace ecs
