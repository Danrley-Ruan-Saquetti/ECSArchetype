#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include "oly/app/detail/state_scoped.hpp"
#include "oly/app/event.hpp"
#include "oly/app/scheduler.hpp"
#include "oly/app/state.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"
#include "oly/ecs/world.hpp"
#include "oly/input/action_map.hpp"

namespace oly::app {
  class Engine;

  // What systems and handlers have in common: clock, pause, reading the world and emitting events.
  class EngineContext {
   protected:
    Engine* engine_;
    ecs::World* world_;

    EngineContext(Engine& engine, ecs::World& world) noexcept
      : engine_(&engine),
        world_(&world) {}

    EventTypeId eventTypeOf(EventKey key) const noexcept;
    // True when a state is active: what the context creates is born scoped.
    bool spawnsScoped() const noexcept;

   public:
    float fixedDeltaTime() const noexcept;
    double time() const noexcept;

    void requestState(StateId state);
    StateId state() const noexcept;

    void pause() noexcept;
    void resume() noexcept;
    bool paused() const noexcept;

    input::ActionMap& actions() const noexcept;

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

    // The event is dispatched in the next round of the current step.
    template <class T>
    void emit(const T& event) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view name = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "emit: event \"%.*s\" not declared", static_cast<int>(name.size()), name.data());

      emitById(type, &event);
    }
    void emitById(EventTypeId type, const void* event);

    Scheduler& schedule() const noexcept;
  };

  // Direct structural changes on the World, for code that runs outside any iteration (handlers, tasks and state hooks).
  class DirectContext : public EngineContext {
   protected:
    DirectContext(Engine& engine, ecs::World& world) noexcept
      : EngineContext(engine, world) {}

   public:
    // With an active state the entity is born scoped (it disappears on a state change) until it receives persist.
    ecs::Entity spawn() {
      if (spawnsScoped()) {
        return world_->spawn(detail::StateScoped{});
      }

      return world_->spawn();
    }
    template <class... Ts>
    ecs::Entity spawn(Ts&&... components) {
      if (spawnsScoped()) {
        return world_->spawn(std::forward<Ts>(components)..., detail::StateScoped{});
      }

      return world_->spawn(std::forward<Ts>(components)...);
    }
    // Removes the scope; the entity must be alive (OLY_ASSERT) and without the tag there is no effect.
    void persist(ecs::Entity entity);
    void destroy(ecs::Entity entity) {
      world_->destroy(entity);
    }
    template <class T>
    void add(ecs::Entity entity, T&& value) {
      world_->add(entity, std::forward<T>(value));
    }
    template <class T>
    void remove(ecs::Entity entity) {
      world_->template remove<T>(entity);
    }
    void addById(ecs::Entity entity, ecs::ComponentId componentId, void* value) {
      world_->addById(entity, componentId, value);
    }
    void removeById(ecs::Entity entity, ecs::ComponentId componentId) {
      world_->removeById(entity, componentId);
    }
  };
}
