#include "oly/app/system.hpp"

#include <string>
#include <utility>
#include <vector>

#include "oly/app/engine.hpp"

namespace oly::app {
  namespace {
    // Dentro de função: a ordem de inicialização estática entre arquivos não importa.
    std::vector<std::pair<std::string, SystemFunction>>& namedSystems() {
      static std::vector<std::pair<std::string, SystemFunction>> systems;
      return systems;
    }
  }

  SystemFunction findNamedSystem(std::string_view name) noexcept {
    for (const auto& [registeredName, function] : namedSystems()) {
      if (registeredName == name) {
        return function;
      }
    }

    return nullptr;
  }

  NamedSystemRegistration::NamedSystemRegistration(std::string_view name, SystemFunction function) {
    namedSystems().emplace_back(std::string(name), function);
  }

  float SystemContext::fixedDeltaTime() const noexcept {
    return static_cast<float>(engine_->fixedDeltaTime());
  }

  double SystemContext::time() const noexcept {
    return engine_->time();
  }

  float SystemContext::interpolation() const noexcept {
    return static_cast<float>(engine_->interpolation());
  }

  void SystemContext::pause() noexcept {
    engine_->pause();
  }

  void SystemContext::resume() noexcept {
    engine_->resume();
  }

  bool SystemContext::paused() const noexcept {
    return engine_->paused();
  }
}
