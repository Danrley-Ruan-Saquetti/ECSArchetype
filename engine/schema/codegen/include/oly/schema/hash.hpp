#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "oly/schema/schema.hpp"

namespace oly::schema {
  // 32-bit FNV-1a over the bytes. Names are ASCII (validation), so the bytes are the UTF-16 code units the TS client
  // hashed before oly-schema existed.
  uint32_t fnv1a32(std::string_view text);

  // `Name{field:type,...};` per component, `Name;` per tag, `!Name{field:type,...};` per event, `@name:kind;` per action
  // and `#name;` per state, each group in order. Changing it changes the hash of every existing schema.
  std::string canonicalForm(const Schema& schema);
}
