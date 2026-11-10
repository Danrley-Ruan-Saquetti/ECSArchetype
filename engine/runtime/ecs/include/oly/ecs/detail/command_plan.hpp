#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "oly/ecs/component.hpp"
#include "oly/ecs/entity.hpp"

namespace oly::ecs::detail {
  enum class CommandOperation : uint8_t { Spawn,
                                          Destroy,
                                          Add,
                                          Remove };

  struct CommandView {
    CommandOperation operation;
    Entity entity;
    ComponentId componentId;
    void* payload; // nullptr em Spawn, Destroy, Remove e em Add de tag
  };

  struct NetComponentOperation {
    ComponentId componentId;
    bool present;
    void* payload; // valor do último Add (nullptr para tags e remoções)
  };

  struct EntityPlan {
    Entity entity;
    uint32_t begin = 0, count = 0; // faixa em CommandPlan::order (ou offset/8 do primeiro registro, se contíguo)
  };

  struct NetEffect {
    bool spawned = false;
    bool destroyed = false;
    std::vector<NetComponentOperation> operations; // ordenado por componentId
  };

  // Guarda só offsets/8 dos registros, para tocar pouca memória; o efeito líquido sai de
  // netEffect, uma entidade por vez.
  struct CommandPlan {
    std::vector<EntityPlan> owned;
    // Na ordem da primeira aparição. Aponta para `owned` ou para os trechos guardados no CommandBuffer,
    // então o plano só vale enquanto o buffer não mudar.
    std::span<const EntityPlan> entities;
    // Vazio quando os comandos de cada entidade já são consecutivos no buffer (caso comum de lotes):
    // então `EntityPlan::begin` é o offset/8 do primeiro registro e basta andar `count` registros.
    // Senão, o offset/8 de cada registro, agrupado por entidade e na ordem de gravação.
    std::vector<uint32_t> order;
    bool contiguous = true;

    CommandPlan() = default;
    CommandPlan(CommandPlan&&) = default;
    CommandPlan(const CommandPlan&) = delete; // a span `entities` da cópia apontaria para o `owned` do original
  };
}
