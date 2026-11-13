#include "oly/schema/generator.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "oly/schema/cpp_emitter.hpp"

namespace {
  std::filesystem::path freshDirectory(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "oly_schema_generator" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    return directory;
  }

  void writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();

    return text.str();
  }

  int run(const std::vector<std::string>& arguments, std::ostream& errors) {
    const std::vector<std::string_view> views(arguments.begin(), arguments.end());

    return oly::schema::runGenerator(views, errors);
  }
}

TEST_CASE("--emit cpp writes the header and the registry into the target directory") {
  const std::filesystem::path directory = freshDirectory("emit_cpp");
  writeText(directory / "game.oly", "component P { a: u8 }\n");
  std::ostringstream errors;

  const int exitCode = run({(directory / "game.oly").string(), "--emit", "cpp=" + (directory / "out").string()}, errors);

  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\n", "game.oly");
  CHECK(exitCode == 0);
  CHECK(errors.str().empty());
  CHECK(readText(directory / "out" / "schema.generated.hpp") == oly::schema::emitCppHeader(schema));
  CHECK(readText(directory / "out" / "schema_registry.generated.cpp") == oly::schema::emitCppRegistry(schema));
}

TEST_CASE("a schema error is reported with the path as given and nothing is written") {
  const std::filesystem::path directory = freshDirectory("schema_error");
  const std::string input = (directory / "game.oly").string();
  writeText(directory / "game.oly", "component a { x: f32 }\n");
  std::ostringstream errors;

  const int exitCode = run({input, "--emit", "cpp=" + (directory / "out").string()}, errors);

  CHECK(exitCode == 1);
  CHECK(errors.str() == input + ":1:11: invalid type name \"a\": use PascalCase ([A-Z][A-Za-z0-9]*)\n");
  CHECK(!std::filesystem::exists(directory / "out"));
}

TEST_CASE("an unknown emitter names the available ones and nothing is written") {
  const std::filesystem::path directory = freshDirectory("unknown_emitter");
  writeText(directory / "game.oly", "tag T\n");
  std::ostringstream errors;

  const int exitCode = run({(directory / "game.oly").string(), "--emit", "cpp=" + (directory / "out").string(), "--emit", "nope=x"}, errors);

  CHECK(exitCode == 1);
  CHECK(errors.str() == "unknown emitter \"nope\"; available: cpp\n");
  CHECK(!std::filesystem::exists(directory / "out"));
}

TEST_CASE("usage errors: no input, no --emit, --emit without a value or without =") {
  for (const std::vector<std::string>& arguments : std::vector<std::vector<std::string>>{{}, {"game.oly"}, {"game.oly", "--emit"}, {"game.oly", "--emit", "cpp"}}) {
    std::ostringstream errors;

    CHECK(run(arguments, errors) == 1);
    CHECK(errors.str().rfind("usage: oly-schema <file.oly> --emit <emitter>=<target>", 0) == 0);
  }
}

TEST_CASE("an unreadable input is reported") {
  std::ostringstream errors;

  CHECK(run({"missing.oly", "--emit", "cpp=out"}, errors) == 1);
  CHECK(errors.str() == "cannot read missing.oly\n");
}
