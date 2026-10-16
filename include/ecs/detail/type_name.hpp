#pragma once

#include <string_view>

namespace ecs::detail {

  // Nome legível de T extraído do nome da função (GCC/Clang: "T = X", MSVC: "typeName<X>").
  template <class T>
  constexpr std::string_view typeName() {
#if defined(_MSC_VER)
    std::string_view s = __FUNCSIG__;
    constexpr std::string_view prefix = "typeName<";
    auto b = s.find(prefix) + prefix.size();
    auto e = s.rfind(">(void)");
    return s.substr(b, e - b);
#else
    std::string_view s = __PRETTY_FUNCTION__;
    constexpr std::string_view marker = "T = ";
    auto b = s.find(marker) + marker.size();
    auto e = s.find_first_of(";]", b);
    return s.substr(b, e - b);
#endif
  }

} // namespace ecs::detail
