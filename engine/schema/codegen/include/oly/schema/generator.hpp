#pragma once

#include <ostream>
#include <span>
#include <string_view>

namespace oly::schema {
  // The oly-schema command line: `<file.oly> --emit <emitter>=<target> [--emit <emitter>=<target> ...]`. Returns 0, or
  // 1 after writing one error line to `errors`. Checks every --emit before writing anything; writes each file whole,
  // creating its directory.
  int runGenerator(std::span<const std::string_view> arguments, std::ostream& errors);
}
