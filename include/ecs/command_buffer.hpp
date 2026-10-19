#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "ecs/entity.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  enum class CommandOp : uint8_t { Spawn,
                                   Destroy,
                                   Add,
                                   Remove };

  struct CommandView {
    CommandOp op;
    Entity e;
    ComponentId cid;
    void* payload; // nullptr se não há payload (Spawn/Destroy/Remove/tag)
  };

  // Passada 1 do flush (R-CB-3): efeito líquido por entidade.
  struct NetComponentOp {
    ComponentId cid;
    bool present; // true: componente presente ao final; false: ausente
    void* payload; // valor do último Add (nullptr para tags e remoções)
  };

  struct EntityPlan {
    Entity e;
    uint32_t begin = 0, count = 0; // faixa em CommandPlan::order (ou offset/8 do primeiro registro, se contíguo)
  };

  // Efeito líquido de uma entidade (R-CB-4); montado sob demanda por CommandBuffer::netEffect.
  struct NetEffect {
    bool spawned = false;
    bool destroyed = false;
    std::vector<NetComponentOp> ops; // ordenado por cid
  };

  // Passada 1: agrupa os comandos por entidade (counting sort estável). Guarda só offsets (em múltiplos
  // de 8) dos registros, para tocar pouca memória; o efeito líquido sai de netEffect, uma entidade por vez.
  struct CommandPlan {
    CommandPlan() = default;
    CommandPlan(CommandPlan&&) = default; // `entities` pode apontar para `owned` ou para os trechos do buffer
    CommandPlan(const CommandPlan&) = delete;

    std::vector<EntityPlan> owned;
    std::span<const EntityPlan> entities; // na ordem da primeira aparição
    // Vazio quando os comandos de cada entidade já são consecutivos no buffer (caso comum de lotes):
    // então `EntityPlan::begin` é o offset/8 do primeiro registro e basta andar `count` registros.
    // Senão, o offset/8 de cada registro, agrupado por entidade e na ordem de gravação.
    std::vector<uint32_t> order;
    bool contiguous = true;
  };

  // Registro (alinhado a 8): [op:u8][pad:7][entity:u64][cid:u32][size:u32][payload alinhado].
  // O bloco é alinhado a 64. Ao crescer, cada payload é realocado pelo seu TypeInfo
  // (memcpy se trivial, moveConstruct + destroy caso contrário): um memmove cego quebraria
  // tipos que apontam para si mesmos (ex.: std::string com SSO).
  class CommandBuffer {
   public:
    explicit CommandBuffer(EntityStore& store) noexcept
      : store_(&store) {}
    ~CommandBuffer();
    CommandBuffer(CommandBuffer&& o) noexcept;
    CommandBuffer& operator=(CommandBuffer&& o) noexcept;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Reserva um índice imediatamente (R-CB-1); a entidade só fica viva no flush.
    Entity spawn();
    void destroy(Entity e);
    template <class T>
    void add(Entity e, T&& value);
    template <class T>
    void remove(Entity e) {
      removeRaw(e, componentId<T>());
    }
    void removeRaw(Entity e, ComponentId cid);

    // Visita os comandos em ordem de gravação.
    template <class F>
    void forEach(F&& f) {
      for (size_t off = 0; off < size_;) {
        CommandView v;
        off = readAt(off, v);
        f(static_cast<const CommandView&>(v));
      }
    }

    // Agrupa os comandos por entidade preservando a ordem (R-CB-3) e calcula o efeito líquido de cada uma (R-CB-4).
    CommandPlan plan() const;
    void netEffect(const CommandPlan& plan, const EntityPlan& ep, NetEffect& out) const;

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
    std::byte* pushRecord(CommandOp op, Entity e, ComponentId cid, uint32_t size, uint32_t align, bool trivial);
    size_t readAt(size_t off, CommandView& out) const noexcept; // devolve o offset do próximo registro
    void grow(size_t need);
    void freeBlock() noexcept;

    EntityStore* store_;
    std::byte* data_ = nullptr;
    size_t size_ = 0, cap_ = 0;
    size_t count_ = 0;
    bool nonTrivial_ = false; // algum payload exige moveConstruct/destroy
    std::vector<Entity> spawned_;

    // Enquanto cada entidade ocupa um trecho consecutivo (caso comum de lotes), os trechos são
    // mantidos na gravação e plan() não precisa reler o buffer. `seen_` é um bitset por slot.
    bool grouped_ = true;
    std::vector<EntityPlan> runs_;
    std::vector<uint64_t> seen_;
  };

  template <class T>
  void CommandBuffer::add(Entity e, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId cid = componentId<U>();

    if constexpr (std::is_empty_v<U>) {
      pushRecord(CommandOp::Add, e, cid, 0, 1, true);
    } else {
      void* p = pushRecord(CommandOp::Add, e, cid, static_cast<uint32_t>(sizeof(U)), static_cast<uint32_t>(alignof(U)),
                           std::is_trivially_copyable_v<U> && std::is_trivially_destructible_v<U>);
      ::new (p) U(std::forward<T>(value));
    }
  }
} // namespace ecs
