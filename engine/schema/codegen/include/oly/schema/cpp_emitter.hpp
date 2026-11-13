#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "oly/schema/emitter.hpp"
#include "oly/schema/schema.hpp"

namespace oly::schema {
  std::string emitCppHeader(const Schema& schema);

  std::string emitCppRegistry(const Schema& schema);

  // `target` is a directory: schema.generated.hpp and schema_registry.generated.cpp.
  std::vector<EmittedFile> emitCpp(const Schema& schema, const std::filesystem::path& target);
}
