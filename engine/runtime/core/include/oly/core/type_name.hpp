#pragma once

#include <string_view>

namespace ecs::detail {
  // Nome legível de T extraído do nome da função (GCC/Clang: "T = X", MSVC: "typeName<X>").
  template <class T>
  constexpr std::string_view typeName() {
#if defined(_MSC_VER)
    std::string_view functionSignature = __FUNCSIG__;
    constexpr std::string_view prefix = "typeName<";
    auto begin = functionSignature.find(prefix) + prefix.size();
    auto end = functionSignature.rfind(">(void)");
    return functionSignature.substr(begin, end - begin);
#else
    std::string_view functionSignature = __PRETTY_FUNCTION__;
    constexpr std::string_view marker = "T = ";
    auto begin = functionSignature.find(marker) + marker.size();
    auto end = functionSignature.find_first_of(";]", begin);
    return functionSignature.substr(begin, end - begin);
#endif
  }
}
