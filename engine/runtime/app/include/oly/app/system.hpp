#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "oly/app/phase.hpp"
#include "oly/ecs/world.hpp"

namespace oly::app {
  class Engine;
  class SystemContext;

  using SystemId = uint32_t;
  using SystemFunction = void (*)(SystemContext& context);

  // Válido só durante a chamada do sistema. Mudanças estruturais são adiadas até o fim da fase.
  class SystemContext {
   private:
    friend class Engine;

    Engine* engine_;
    ecs::World* world_;
    ecs::CommandBuffer* commands_;
    Phase phase_;
    SystemId systemId_;
    std::string_view name_;
    void* userData_;

    SystemContext(Engine& engine, ecs::World& world, ecs::CommandBuffer& commands, Phase phase, SystemId systemId, std::string_view name, void* userData) noexcept
      : engine_(&engine),
        world_(&world),
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

    float fixedDeltaTime() const noexcept;
    double time() const noexcept;
    float interpolation() const noexcept;

    void pause() noexcept;
    void resume() noexcept;
    bool paused() const noexcept;

    template <class... Ts, class... Filters>
    ecs::Query<Ts...>& query(Filters... filters) {
      return world_->template query<Ts...>(filters...);
    }
    template <class T>
    T& get(ecs::Entity entity) {
      return world_->template get<T>(entity);
    }
    template <class T>
    T* tryGet(ecs::Entity entity) noexcept {
      return world_->template tryGet<T>(entity);
    }
    template <class T>
    bool has(ecs::Entity entity) const noexcept {
      return world_->template has<T>(entity);
    }
    bool isAlive(ecs::Entity entity) const noexcept {
      return world_->isAlive(entity);
    }

    ecs::Entity spawn() {
      return commands_->spawn();
    }
    template <class... Ts>
    ecs::Entity spawn(Ts&&... components) {
      ecs::Entity entity = commands_->spawn();
      (commands_->add(entity, std::forward<Ts>(components)), ...);
      return entity;
    }
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
