#pragma once

#include <cstdint>
#include <string_view>

#include "oly/app/event.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"
#include "oly/ecs/world.hpp"
#include "oly/input/action_map.hpp"

namespace oly::app {
  class Engine;

  // O que sistemas e handlers têm em comum: relógio, pausa, leitura do mundo e emissão de eventos.
  class EngineContext {
   protected:
    Engine* engine_;
    ecs::World* world_;

    EngineContext(Engine& engine, ecs::World& world) noexcept
      : engine_(&engine),
        world_(&world) {}

    EventTypeId eventTypeOf(EventKey key) const noexcept;

   public:
    float fixedDeltaTime() const noexcept;
    double time() const noexcept;

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

    // O evento é despachado na rodada seguinte do passo em curso.
    template <class T>
    void emit(const T& event) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view name = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "emit: evento \"%.*s\" não declarado", static_cast<int>(name.size()), name.data());

      emitById(type, &event);
    }
    void emitById(EventTypeId type, const void* event);
  };
}
