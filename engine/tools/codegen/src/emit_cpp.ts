import type { ActionKind, ActionType, ComponentLayout, EventType, ScalarType, Schema, SchemaType } from "@oly/core";

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

function emitStruct(type: SchemaType | EventType): string {
  if (type.kind === "tag") {
    return `struct ${type.name} {};\n`;
  }

  const fields = type.layout.fields.map((field) => `  ${cppTypes[field.type]} ${field.name};\n`).join("");

  return `struct ${type.name} {\n${fields}};\n${emitLayoutAsserts(type.name, type.layout)}`;
}

function emitLayoutAsserts(name: string, layout: ComponentLayout): string {
  const { size, alignment, fields } = layout;
  const message = `"esquema: layout de ${name} diverge do TS"`;
  const sizeAssert = `static_assert(sizeof(${name}) == ${size} && alignof(${name}) == ${alignment}, ${message});\n`;
  const offsetAsserts = fields
    .map((field) => `static_assert(offsetof(${name}, ${field.name}) == ${field.offset}, ${message});\n`)
    .join("");

  return sizeAssert + offsetAsserts;
}

const cppActionHandles: Readonly<Record<ActionKind, string>> = {
  button: "ButtonAction",
  axis2d: "Axis2dAction",
  pointer: "PointerAction",
};

const cppActionKinds: Readonly<Record<ActionKind, string>> = {
  button: "Button",
  axis2d: "Axis2d",
  pointer: "Pointer",
};

function emitActions(actions: readonly ActionType[]): string {
  if (actions.length === 0) {
    return "";
  }

  const handles = actions
    .map(
      (action) =>
        `  inline constexpr oly::input::${cppActionHandles[action.actionKind]} ${action.name}{${action.schemaIndex}};\n`,
    )
    .join("");

  return `\nnamespace actions {\n${handles}}\n`;
}

export function emitHeader(schema: Schema): string {
  const structs = [...schema.types, ...schema.events].map(emitStruct).join("\n");
  const inputInclude = schema.actions.length === 0 ? "" : '#include "oly/input/action_map.hpp"\n';

  return `// Gerado por oly-codegen: não edite.\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n#include "oly/ecs/entity.hpp"\n${inputInclude}\n${structs}${emitActions(schema.actions)}`;
}

function emitEventsRegistry(schema: Schema): string {
  if (schema.events.length === 0) {
    return `  std::span<const SchemaEvent> schemaEvents() {
    return {};
  }
`;
  }

  const entries = schema.events
    .map(
      (event) =>
        `      {"${event.name}", sizeof(${event.name}), alignof(${event.name}), [](oly::app::Engine& engine) { engine.declareEvent<${event.name}>("${event.name}"); }}`,
    )
    .join(",\n");

  return `  std::span<const SchemaEvent> schemaEvents() {
    static const SchemaEvent events[] = {
${entries},
    };
    return events;
  }
`;
}

function emitActionsRegistry(schema: Schema): string {
  if (schema.actions.length === 0) {
    return `  std::span<const SchemaAction> schemaActions() {
    return {};
  }
`;
  }

  const entries = schema.actions
    .map((action) => `      {"${action.name}", oly::input::ActionKind::${cppActionKinds[action.actionKind]}}`)
    .join(",\n");

  return `  std::span<const SchemaAction> schemaActions() {
    static const SchemaAction actions[] = {
${entries},
    };
    return actions;
  }
`;
}

export function emitRegistry(schema: Schema): string {
  const ids = schema.types.map((type) => `      oly::ecs::componentId<${type.name}>()`).join(",\n");

  return `// Gerado por oly-codegen: não edite.
#include "oly/app/engine.hpp"
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

${emitEventsRegistry(schema)}
${emitActionsRegistry(schema)}}
`;
}
