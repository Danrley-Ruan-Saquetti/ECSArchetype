#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "oly/app/context.hpp"
#include "oly/app/phase.hpp"
#include "oly/ecs/world.hpp"

namespace oly::app {
  class Engine;
  class SystemContext;

  using SystemId = uint32_t;
  using SystemFunction = void (*)(SystemContext& context);

  // Válido só durante a chamada do sistema. Mudanças estruturais são adiadas até o fim da fase.
  class SystemContext : public EngineContext {
   private:
    friend class Engine;

    ecs::CommandBuffer* commands_;
    Phase phase_;
    SystemId systemId_;
    std::string_view name_;
    void* userData_;

    SystemContext(Engine& engine, ecs::World& world, ecs::CommandBuffer& commands, Phase phase, SystemId systemId, std::string_view name, void* userData) noexcept
      : EngineContext(engine, world),
        commands_(&commands),
        phase_(phase),
        systemId_(systemId),
        name_(name),
        userData_(userData) {}

   public:
    Phase phase() const noexcept {
      return phase_;
    }
    SystemId systemId() const noexcept {
      return systemId_;
    }
    std::string_view name() const noexcept {
      return name_;
    }
    void* userData() const noexcept {
      return userData_;
    }

    float interpolation() const noexcept;

    // Com estado ativo a entidade nasce com escopo (some na troca de estado) até receber persist.
    ecs::Entity spawn();
    template <class... Ts>
    ecs::Entity spawn(Ts&&... components) {
      ecs::Entity entity = spawn();
      (commands_->add(entity, std::forward<Ts>(components)), ...);
      return entity;
    }
    // Adiado como o remove: sem efeito se a entidade não tiver a tag.
    void persist(ecs::Entity entity);
    void destroy(ecs::Entity entity) {
      commands_->destroy(entity);
    }
    template <class T>
    void add(ecs::Entity entity, T&& value) {
      commands_->add(entity, std::forward<T>(value));
    }
    template <class T>
    void remove(ecs::Entity entity) {
      commands_->template remove<T>(entity);
    }
    void addById(ecs::Entity entity, ecs::ComponentId componentId, void* value) {
      commands_->addById(entity, componentId, value);
    }
    void removeById(ecs::Entity entity, ecs::ComponentId componentId) {
      commands_->removeById(entity, componentId);
    }
  };

  // nullptr se o nome não existe.
  SystemFunction findNamedSystem(std::string_view name) noexcept;

  struct NamedSystemRegistration {
    NamedSystemRegistration(std::string_view name, SystemFunction function);
  };
}

#define OLY_SYSTEM(name)                                                           \
  void name(oly::app::SystemContext& context);                                     \
  static const oly::app::NamedSystemRegistration name##Registration(#name, &name); \
  void name([[maybe_unused]] oly::app::SystemContext& context)
