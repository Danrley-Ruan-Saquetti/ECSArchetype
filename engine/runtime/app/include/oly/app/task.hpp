#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <type_traits>

#include "oly/core/type_name.hpp"

namespace oly::app {
  class TaskContext;

  using TaskFunction = void (*)(TaskContext& context);
  using TaskDiscardFunction = void (*)(void* payload);
  using PayloadKey = uint32_t;

  inline constexpr uint32_t InvalidTaskSlot = 0xFFFFFFFFu;
  inline constexpr PayloadKey NoPayloadKey = 0xFFFFFFFFu;
  inline constexpr std::size_t MaxTaskPayloadSize = 32;
  inline constexpr std::size_t MaxTaskPayloadAlignment = 8;

  // The default handle references no task.
  struct TaskHandle {
    uint32_t slot = InvalidTaskSlot;
    uint32_t generation = 0;

    bool operator==(const TaskHandle&) const = default;
  };

  namespace detail {
    PayloadKey nextPayloadKey() noexcept;
  }

  // Dense per process, like eventKey.
  template <class P>
  PayloadKey payloadKey() noexcept {
    static const PayloadKey key = detail::nextPayloadKey();
    return key;
  }

  struct TaskPayload {
    alignas(MaxTaskPayloadAlignment) std::byte bytes[MaxTaskPayloadSize] = {};
    PayloadKey key = NoPayloadKey;
    std::string_view typeName;

    template <class P>
    static TaskPayload of(const P& value) {
      static_assert(std::is_trivially_copyable_v<P>, "TaskPayload: the payload must be trivially copyable");
      static_assert(sizeof(P) <= MaxTaskPayloadSize, "TaskPayload: payload larger than 32 bytes");
      static_assert(alignof(P) <= MaxTaskPayloadAlignment, "TaskPayload: payload alignment larger than 8");

      TaskPayload payload;
      std::memcpy(payload.bytes, &value, sizeof(P));
      payload.key = payloadKey<P>();
      payload.typeName = oly::core::typeName<P>();

      return payload;
    }
  };
}
