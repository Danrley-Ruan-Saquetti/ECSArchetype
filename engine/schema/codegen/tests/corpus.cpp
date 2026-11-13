#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "oly/schema/schema.hpp"

namespace {
  const std::filesystem::path corpus = OLY_SCHEMA_CORPUS;

  std::vector<std::filesystem::path> schemaFilesIn(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
      if (entry.path().extension() == ".oly") {
        files.push_back(entry.path());
      }
    }

    std::sort(files.begin(), files.end());
    return files;
  }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();

    return text.str();
  }
}

TEST_CASE("every valid corpus file parses") {
  const std::vector<std::filesystem::path> files = schemaFilesIn(corpus / "valid");
  REQUIRE(!files.empty());

  for (const std::filesystem::path& file : files) {
    INFO(file.filename().string());
    CHECK_NOTHROW(oly::schema::parseSchema(readText(file), file.filename().string()));
  }
}

TEST_CASE("every invalid corpus file fails with the error of its first line") {
  const std::vector<std::filesystem::path> files = schemaFilesIn(corpus / "invalid");
  REQUIRE(!files.empty());

  for (const std::filesystem::path& file : files) {
    const std::string text = readText(file);
    const std::string expectPrefix = "# expect: ";
    REQUIRE(text.rfind(expectPrefix, 0) == 0);
    const std::string expected = file.filename().string() + ":" + text.substr(expectPrefix.size(), text.find('\n') - expectPrefix.size());

    INFO(file.filename().string());
    CHECK_THROWS_WITH_AS(oly::schema::parseSchema(text, file.filename().string()), expected.c_str(), oly::schema::SchemaError);
  }
}
