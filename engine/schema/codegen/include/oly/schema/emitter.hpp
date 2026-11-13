#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "oly/schema/schema.hpp"

namespace oly::schema {
  struct EmittedFile {
    std::filesystem::path path;
    std::string contents;
  };

  // `target` is what follows `--emit <name>=` on the command line: a directory or a file, as each emitter defines.
  // Throws SchemaError when the schema cannot be expressed in the emitter's language.
  using EmitFunction = std::vector<EmittedFile> (*)(const Schema& schema, const std::filesystem::path& target);

  struct Emitter {
    std::string_view name;
    EmitFunction emit;
  };

  // Called from a namespace-scope initializer in a client's emitter module, which is an OBJECT library so that the
  // linker keeps it. Returns true so that the call can initialize a constant.
  bool registerEmitter(Emitter emitter);

  // The C++ emitter first (C++ is the engine language, not a client), then the registered ones in registration order.
  std::span<const Emitter> emitters();
}
