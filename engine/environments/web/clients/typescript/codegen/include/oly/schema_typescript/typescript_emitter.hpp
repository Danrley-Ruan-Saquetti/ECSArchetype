#pragma once

#include <string>

#include "oly/schema/schema.hpp"

namespace oly::schema_typescript {
  // The schema.generated.ts module for @oly/core: one exported constant per entry and the default Schema. Throws
  // oly::schema::SchemaError for an action or state whose name cannot be a TypeScript constant.
  std::string emitTypescript(const oly::schema::Schema& schema);
}
