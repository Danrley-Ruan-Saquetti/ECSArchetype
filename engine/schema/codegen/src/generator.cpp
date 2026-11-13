#include "oly/schema/generator.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "oly/schema/emitter.hpp"
#include "oly/schema/schema.hpp"

namespace oly::schema {
  namespace {
    constexpr std::string_view usage = "usage: oly-schema <file.oly> --emit <emitter>=<target> [--emit <emitter>=<target> ...]\n";

    struct EmitRequest {
      std::string name;
      std::string target;
    };

    std::optional<std::vector<EmitRequest>> parseEmitRequests(std::span<const std::string_view> arguments) {
      std::vector<EmitRequest> requests;
      for (std::size_t index = 1; index < arguments.size(); index += 2) {
        if (arguments[index] != "--emit" || index + 1 >= arguments.size()) {
          return std::nullopt;
        }

        const std::string_view value = arguments[index + 1];
        const std::size_t separator = value.find('=');
        if (separator == std::string_view::npos || separator == 0) {
          return std::nullopt;
        }

        requests.push_back({std::string(value.substr(0, separator)), std::string(value.substr(separator + 1))});
      }

      if (requests.empty()) {
        return std::nullopt;
      }

      return requests;
    }

    const Emitter* findEmitter(std::string_view name) {
      for (const Emitter& emitter : emitters()) {
        if (emitter.name == name) {
          return &emitter;
        }
      }

      return nullptr;
    }

    std::string availableEmitters() {
      std::string names;
      for (const Emitter& emitter : emitters()) {
        if (!names.empty()) {
          names += ", ";
        }

        names += emitter.name;
      }

      return names;
    }

    std::optional<std::string> readText(const std::string& path) {
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        return std::nullopt;
      }

      std::ostringstream text;
      text << file.rdbuf();

      return text.str();
    }

    bool writeFile(const EmittedFile& file) {
      if (!file.path.parent_path().empty()) {
        std::error_code error;
        std::filesystem::create_directories(file.path.parent_path(), error);
        if (error) {
          return false;
        }
      }

      std::ofstream output(file.path, std::ios::binary);
      output << file.contents;
      output.flush();

      return static_cast<bool>(output);
    }
  }

  int runGenerator(std::span<const std::string_view> arguments, std::ostream& errors) {
    const std::optional<std::vector<EmitRequest>> requests = arguments.empty() ? std::nullopt : parseEmitRequests(arguments);
    if (!requests) {
      errors << usage;
      return 1;
    }

    for (const EmitRequest& request : *requests) {
      if (findEmitter(request.name) == nullptr) {
        errors << "unknown emitter \"" << request.name << "\"; available: " << availableEmitters() << "\n";
        return 1;
      }
    }

    const std::string input(arguments[0]);
    const std::optional<std::string> text = readText(input);
    if (!text) {
      errors << "cannot read " << input << "\n";
      return 1;
    }

    std::vector<EmittedFile> files;
    try {
      const Schema schema = parseSchema(*text, input);
      for (const EmitRequest& request : *requests) {
        for (EmittedFile& file : findEmitter(request.name)->emit(schema, request.target)) {
          files.push_back(std::move(file));
        }
      }
    } catch (const SchemaError& error) {
      errors << error.what() << "\n";
      return 1;
    }

    for (const EmittedFile& file : files) {
      if (!writeFile(file)) {
        errors << "cannot write " << file.path.string() << "\n";
        return 1;
      }
    }

    return 0;
  }
}
