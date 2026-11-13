#include <iostream>
#include <string_view>
#include <vector>

#include "oly/schema/generator.hpp"

int main(int argumentCount, char** argumentValues) {
  const std::vector<std::string_view> arguments(argumentValues + 1, argumentValues + argumentCount);

  return oly::schema::runGenerator(arguments, std::cerr);
}
