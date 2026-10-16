#pragma once
#include <cstdint>
#include <new>
#include <string_view>
#include <type_traits>
#include <utility>

#include <ecs/detail/type_name.hpp>

namespace ecs {

using ComponentId = uint32_t;

struct TypeInfo {
    uint32_t size;    // 0 para tags
    uint32_t align;
    bool trivial;     // is_trivially_copyable_v && is_trivially_destructible_v
    bool tag;         // is_empty_v
    void (*moveConstruct)(void* dst, void* src) noexcept;  // nullptr se trivial
    void (*destroy)(void* p) noexcept;                     // nullptr se trivial
    std::string_view name;                                 // para diagnóstico
};

const TypeInfo& typeInfo(ComponentId id);
uint32_t registeredTypeCount() noexcept;

namespace detail {

ComponentId registerType(const TypeInfo& info);

template <class U>
TypeInfo makeTypeInfo() {
    TypeInfo ti{};
    ti.tag = std::is_empty_v<U>;
    ti.size = ti.tag ? 0 : static_cast<uint32_t>(sizeof(U));
    ti.align = static_cast<uint32_t>(alignof(U));
    ti.trivial = ti.tag || (std::is_trivially_copyable_v<U> && std::is_trivially_destructible_v<U>);
    if (!ti.trivial) {
        ti.moveConstruct = [](void* dst, void* src) noexcept { ::new (dst) U(std::move(*static_cast<U*>(src))); };
        ti.destroy = [](void* p) noexcept { static_cast<U*>(p)->~U(); };
    }
    ti.name = typeName<U>();
    return ti;
}

}  // namespace detail

namespace detail {
// O static fica em U (já sem cvref) para que componentId<const A&> e componentId<A> compartilhem o ID.
template <class U>
ComponentId componentIdOf() {
    static_assert(std::is_nothrow_move_constructible_v<U>, "componente exige move-constructor noexcept (R-TI-3)");
    static_assert(alignof(U) <= 64, "componente exige alignof <= 64 (R-TI-3)");
    static const ComponentId id = registerType(makeTypeInfo<U>());
    return id;
}
}  // namespace detail

template <class T>
ComponentId componentId() {
    return detail::componentIdOf<std::remove_cvref_t<T>>();
}

}  // namespace ecs
