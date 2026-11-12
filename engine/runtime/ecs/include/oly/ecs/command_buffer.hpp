#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "oly/ecs/component.hpp"
#include "oly/ecs/detail/command_plan.hpp"
#include "oly/ecs/detail/entity_store.hpp"
#include "oly/ecs/entity.hpp"

namespace oly::ecs {
  class World;

  namespace detail {
    struct CommandBufferTestAccess;
  }

  // Record (8-aligned): [operation:u8][payloadAlignment:u8][pad:6][entity:u64][componentId:u32][size:u32][aligned payload].
  // The block is 64-aligned. On growth, each payload is relocated through its TypeInfo
  // (memcpy if trivial, moveConstruct + destroy otherwise): a blind memmove would break
  // self-referencing types (e.g. std::string with SSO).
  class CommandBuffer {
   private:
    friend class World;
    friend struct detail::CommandBufferTestAccess;

    detail::EntityStore* store_;
    std::byte* data_ = nullptr;
    size_t size_ = 0, capacity_ = 0;
    size_t count_ = 0;
    bool hasNonTrivialPayload_ = false;
    std::vector<Entity> spawned_;

    // While each entity occupies a consecutive run (the common batch case), the runs are
    // kept during recording and plan() does not need to reread the buffer. `seen_` is a per-slot bitset.
    bool grouped_ = true;
    std::vector<detail::EntityPlan> runs_;
    std::vector<uint64_t> seen_;

   public:
    ~CommandBuffer();
    CommandBuffer(CommandBuffer&& other) noexcept;
    CommandBuffer& operator=(CommandBuffer&& other) noexcept;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Reserves an index immediately (R-CB-1); the entity only becomes alive on flush.
    Entity spawn();
    void destroy(Entity entity);

    template <class T>
    void add(Entity entity, T&& value);
    template <class T>
    void remove(Entity entity) {
      removeById(entity, componentId<T>());
    }

    // Moves from `value` (the caller still owns the moved-from object); ignored for tags.
    void addById(Entity entity, ComponentId componentId, void* value);
    void removeById(Entity entity, ComponentId componentId);

    size_t commandCount() const noexcept {
      return count_;
    }
    bool empty() const noexcept {
      return count_ == 0;
    }

    // Destroys the non-trivial payloads (moved-from ones included), empties the buffer and forgets the spawns
    // (already materialized or released by the flush).
    void clear() noexcept;

   private:
    explicit CommandBuffer(detail::EntityStore& store) noexcept
      : store_(&store) {}

    // Visits the commands in recording order.
    template <class F>
    void forEach(F&& callback) const {
      for (size_t offset = 0; offset < size_;) {
        detail::CommandView command;
        offset = readAt(offset, command);
        callback(static_cast<const detail::CommandView&>(command));
      }
    }

    detail::CommandPlan plan() const;
    void netEffect(const detail::CommandPlan& plan, const detail::EntityPlan& entityPlan, detail::NetEffect& out) const;

    const detail::EntityStore* store() const noexcept {
      return store_;
    }

    std::byte* pushRecord(detail::CommandOperation operation, Entity entity, ComponentId componentId, uint32_t size, uint32_t alignment, bool trivial);
    size_t readAt(size_t offset, detail::CommandView& out) const noexcept; // returns the offset of the next record
    void grow(size_t need);
    void freeBlock() noexcept;
    void releasePendingSpawns() noexcept;
  };

  template <class T>
  void CommandBuffer::add(Entity entity, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId componentId = oly::ecs::componentId<U>();

    if constexpr (std::is_empty_v<U>) {
      pushRecord(detail::CommandOperation::Add, entity, componentId, 0, 1, true);
    } else {
      void* payload = pushRecord(detail::CommandOperation::Add, entity, componentId, static_cast<uint32_t>(sizeof(U)), static_cast<uint32_t>(alignof(U)),
                                 std::is_trivially_copyable_v<U> && std::is_trivially_destructible_v<U>);
      ::new (payload) U(std::forward<T>(value));
    }
  }
}
