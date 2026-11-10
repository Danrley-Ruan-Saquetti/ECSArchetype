#include "oly/core/assert.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace oly::core {
  namespace {
    void defaultHandler(const char* message) noexcept {
      std::fprintf(stderr, "%s\n", message);
      std::fflush(stderr);
      std::abort();
    }

    AssertHandler g_handler = &defaultHandler;
  }

  AssertHandler setAssertHandler(AssertHandler handler) noexcept {
    AssertHandler previous = g_handler;
    g_handler = handler ? handler : &defaultHandler;
    return previous;
  }

  namespace detail {
    void assertFail(const char* expression, const char* file, int line, const char* format, ...) noexcept {
      char message[256];
      va_list arguments;
      va_start(arguments, format);
      std::vsnprintf(message, sizeof message, format, arguments);
      va_end(arguments);

      char fullMessage[512];
      std::snprintf(fullMessage, sizeof fullMessage, "OLY_ASSERT failed: %s | %s (%s:%d)", expression, message, file, line);

      g_handler(fullMessage);
      std::abort();
    }
  }
}
