#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "oly/core/align.hpp"
#include "oly/core/small_map.hpp"

TEST_CASE("SmallMap insere, encontra e mantém a ordem das chaves") {
  oly::core::SmallMap<uint32_t, int> map;

  map.insert(7, 70);
  map.insert(3, 30);
  map.insert(5, 50);

  REQUIRE(map.find(5) != nullptr);
  CHECK(*map.find(5) == 50);
  CHECK(map.find(4) == nullptr);

  std::vector<uint32_t> keys;
  for (auto& [key, value] : map) {
    keys.push_back(key);
  }
  CHECK(keys == std::vector<uint32_t>{3, 5, 7});
}

TEST_CASE("alignUp arredonda para o próximo múltiplo") {
  CHECK(oly::core::alignUp(0, 64) == 0);
  CHECK(oly::core::alignUp(1, 64) == 64);
  CHECK(oly::core::alignUp(64, 64) == 64);
  CHECK(oly::core::alignUp(65, 8) == 72);
}
