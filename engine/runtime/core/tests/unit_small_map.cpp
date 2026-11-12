#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "oly/core/align.hpp"
#include "oly/core/small_map.hpp"

TEST_CASE("SmallMap finds keys inserted out of order") {
  oly::core::SmallMap<uint32_t, int> map;

  map.insert(7, 70);
  map.insert(3, 30);
  map.insert(5, 50);

  REQUIRE(map.find(5) != nullptr);
  CHECK(*map.find(5) == 50);
  CHECK(map.find(4) == nullptr);
}

TEST_CASE("SmallMap iterates keys in ascending order") {
  oly::core::SmallMap<uint32_t, int> map;
  map.insert(7, 70);
  map.insert(3, 30);
  map.insert(5, 50);

  std::vector<uint32_t> keys;
  for (auto& [key, value] : map) {
    keys.push_back(key);
  }

  CHECK(keys == std::vector<uint32_t>{3, 5, 7});
}

TEST_CASE("alignUp rounds up to the next multiple") {
  CHECK(oly::core::alignUp(0, 64) == 0);
  CHECK(oly::core::alignUp(1, 64) == 64);
  CHECK(oly::core::alignUp(64, 64) == 64);
  CHECK(oly::core::alignUp(65, 8) == 72);
}
