import type { ComponentType, ScalarType, Schema, SchemaType } from "@oly/core";

const cppTypes: Readonly<Record<ScalarType, string>> = {
  f32: "float",
  f64: "double",
  i8: "int8_t",
  u8: "uint8_t",
  i16: "int16_t",
  u16: "uint16_t",
  i32: "int32_t",
  u32: "uint32_t",
  entity: "oly::ecs::Entity",
};

function emitStruct(type: SchemaType): string {
  if (type.kind === "tag") {
    return `struct ${type.name} {};\n`;
  }

  const fields = type.layout.fields.map((field) => `  ${cppTypes[field.type]} ${field.name};\n`).join("");

  return `struct ${type.name} {\n${fields}};\n${emitLayoutAsserts(type)}`;
}

function emitLayoutAsserts(type: ComponentType): string {
  const { size, alignment, fields } = type.layout;
  const message = `"esquema: layout de ${type.name} diverge do TS"`;
  const sizeAssert = `static_assert(sizeof(${type.name}) == ${size} && alignof(${type.name}) == ${alignment}, ${message});\n`;
  const offsetAsserts = fields
    .map((field) => `static_assert(offsetof(${type.name}, ${field.name}) == ${field.offset}, ${message});\n`)
    .join("");

  return sizeAssert + offsetAsserts;
}

export function emitHeader(schema: Schema): string {
  const structs = schema.types.map(emitStruct).join("\n");

  return `// Gerado por oly-codegen: não edite.\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n#include "oly/ecs/entity.hpp"\n\n${structs}`;
}

export function emitRegistry(schema: Schema): string {
  const ids = schema.types.map((type) => `      oly::ecs::componentId<${type.name}>()`).join(",\n");

  return `// Gerado por oly-codegen: não edite.
#include "oly/bridge/schema_registry.hpp"
#include "schema.generated.hpp"

namespace oly::bridge {
  uint32_t schemaHash() noexcept {
    return 0x${schema.hash.toString(16)}u;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
    static const oly::ecs::ComponentId ids[] = {
${ids}};
    return ids;
  }
}
`;
}
