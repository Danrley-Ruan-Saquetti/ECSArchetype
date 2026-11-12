#pragma once

#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

#include "oly/core/type_name.hpp"

namespace oly::ecs {
  using ComponentId = uint32_t;

  struct TypeInfo {
    uint32_t size;
    uint32_t alignment;
    bool trivial;
    bool tag;
    void (*moveConstruct)(void* destination, void* source) noexcept; // nullptr if trivial
    void (*destroy)(void* object) noexcept; // nullptr if trivial
    std::string_view name;
  };

  // `id` must be less than registeredTypeCount() (not checked). The reference stays valid until the
  // end of the process.
  const TypeInfo& typeInfo(ComponentId id);
  uint32_t registeredTypeCount() noexcept;

  namespace detail {
    ComponentId registerType(const TypeInfo& info);

    template <class U>
    TypeInfo makeTypeInfo() {
      TypeInfo info{};
      info.tag = std::is_empty_v<U>;
      info.size = info.tag ? 0 : static_cast<uint32_t>(sizeof(U));
      info.alignment = static_cast<uint32_t>(alignof(U));
      info.trivial = info.tag || (std::is_trivially_copyable_v<U> && std::is_trivially_destructible_v<U>);
      info.name = core::typeName<U>();

      if (!info.trivial) {
        info.moveConstruct = [](void* destination, void* source) noexcept { ::new (destination) U(std::move(*static_cast<U*>(source))); };
        info.destroy = [](void* object) noexcept { static_cast<U*>(object)->~U(); };
      }

      return info;
    }

    // The static lives in U (already without cvref) so that componentId<const A&> and componentId<A> share the ID.
    template <class U>
    ComponentId componentIdOf() {
      static_assert(std::is_nothrow_move_constructible_v<U>, "component requires a noexcept move constructor (R-TI-3)");
      static_assert(alignof(U) <= 64, "component requires alignof <= 64 (R-TI-3)");

      static const ComponentId id = registerType(makeTypeInfo<U>());
      return id;
    }
  }

  // Dense ID, assigned on the first call for each type: it depends on the order of those calls,
  // so it is not suitable for persisting. Ignores const, volatile and reference.
  template <class T>
  ComponentId componentId() {
    return detail::componentIdOf<std::remove_cvref_t<T>>();
  }
}
