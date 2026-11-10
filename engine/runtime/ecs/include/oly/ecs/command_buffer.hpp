#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "oly/ecs/entity.hpp"
#include "oly/ecs/type_info.hpp"

namespace oly::ecs {
  enum class CommandOperation : uint8_t { Spawn,
                                   Destroy,
                                   Add,
                                   Remove };

  struct CommandView {
    CommandOperation operation;
    Entity entity;
    ComponentId componentId;
    void* payload; // nullptr se não há payload (Spawn/Destroy/Remove/tag)
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
    std::span<const EntityPlan> entities; // na ordem da primeira aparição
    // Vazio quando os comandos de cada entidade já são consecutivos no buffer (caso comum de lotes):
    // então `EntityPlan::begin` é o offset/8 do primeiro registro e basta andar `count` registros.
    // Senão, o offset/8 de cada registro, agrupado por entidade e na ordem de gravação.
    std::vector<uint32_t> order;
    bool contiguous = true;

    CommandPlan() = default;
    CommandPlan(CommandPlan&&) = default;
    CommandPlan(const CommandPlan&) = delete; // a span `entities` da cópia apontaria para o `owned` do original
  };

  // Registro (alinhado a 8): [operation:u8][payloadAlignment:u8][pad:6][entity:u64][componentId:u32][size:u32][payload alinhado].
  // O bloco é alinhado a 64. Ao crescer, cada payload é realocado pelo seu TypeInfo
  // (memcpy se trivial, moveConstruct + destroy caso contrário): um memmove cego quebraria
  // tipos que apontam para si mesmos (ex.: std::string com SSO).
  class CommandBuffer {
   private:
    EntityStore* store_;
    std::byte* data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;
    size_t count_ = 0;
    bool hasNonTrivialPayload_ = false;
    std::vector<Entity> spawned_;

    // Enquanto cada entidade ocupa um trecho consecutivo (caso comum de lotes), os trechos são
    // mantidos na gravação e plan() não precisa reler o buffer. `seen_` é um bitset por slot.
    bool grouped_ = true;
    std::vector<EntityPlan> runs_;
    std::vector<uint64_t> seen_;

   public:
    explicit CommandBuffer(EntityStore& store) noexcept
      : store_(&store) {}
    ~CommandBuffer();
    CommandBuffer(CommandBuffer&& other) noexcept;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Reserva um índice imediatamente (R-CB-1); a entidade só fica viva no flush.
    Entity spawn();
    void destroy(Entity entity);
    template <class T>
    void add(Entity entity, T&& value);
    template <class T>
    void remove(Entity entity) {
      removeRaw(entity, componentId<T>());
    }
    // Move de `value` (quem chamou segue dono do moved-from); ignorado para tags.
    void addRaw(Entity entity, ComponentId componentId, void* value);
    void removeRaw(Entity entity, ComponentId componentId);

    // Visita os comandos em ordem de gravação.
    template <class F>
    void forEach(F&& callback) {
      for (size_t offset = 0; offset < size_;) {
        CommandView command;
        offset = readAt(offset, command);
        callback(static_cast<const CommandView&>(command));
      }
    }

    CommandPlan plan() const;
    void netEffect(const CommandPlan& plan, const EntityPlan& entityPlan, NetEffect& out) const;

    size_t commandCount() const noexcept {
      return count_;
    }
    bool empty() const noexcept {
      return count_ == 0;
    }
    const EntityStore* store() const noexcept {
      return store_;
    }

    // Destrói os payloads não triviais (inclusive moved-from), zera o buffer e esquece os spawns
    // (já materializados ou liberados pelo flush).
    void clear() noexcept;

   private:
    std::byte* pushRecord(CommandOperation operation, Entity entity, ComponentId componentId, uint32_t size, uint32_t alignment, bool trivial);
    size_t readAt(size_t offset, CommandView& out) const noexcept; // devolve o offset do próximo registro
    void grow(size_t need);
    void freeBlock() noexcept;
    void releasePendingSpawns() noexcept;
  };

  template <class T>
  void CommandBuffer::add(Entity entity, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId componentId = oly::ecs::componentId<U>();

    if constexpr (std::is_empty_v<U>) {
      pushRecord(CommandOperation::Add, entity, componentId, 0, 1, true);
    } else {
      void* payload = pushRecord(CommandOperation::Add, entity, componentId, static_cast<uint32_t>(sizeof(U)), static_cast<uint32_t>(alignof(U)),
                                 std::is_trivially_copyable_v<U> && std::is_trivially_destructible_v<U>);
      ::new (payload) U(std::forward<T>(value));
    }
  }
}
