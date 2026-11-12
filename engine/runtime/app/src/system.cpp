#include "oly/app/system.hpp"

#include <string>
#include <utility>
#include <vector>

#include "oly/app/engine.hpp"

namespace oly::app {
  namespace {
    // Estático local, não global: OLY_SYSTEM registra durante a inicialização estática de outros arquivos, cuja
    // ordem relativa a este não é definida.
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

  ecs::Entity SystemContext::spawn() {
    const ecs::Entity entity = commands_->spawn();
    if (spawnsScoped()) {
      commands_->add(entity, detail::StateScoped{});
    }

    return entity;
  }

  void SystemContext::persist(ecs::Entity entity) {
    commands_->remove<detail::StateScoped>(entity);
  }

  float SystemContext::interpolation() const noexcept {
    return static_cast<float>(engine_->interpolation());
  }
}
