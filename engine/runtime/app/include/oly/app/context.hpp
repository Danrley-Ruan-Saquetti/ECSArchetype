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

  // O que sistemas e handlers têm em comum: relógio, pausa, leitura do mundo e emissão de eventos.
  class EngineContext {
   protected:
    Engine* engine_;
    ecs::World* world_;

    EngineContext(Engine& engine, ecs::World& world) noexcept
      : engine_(&engine),
        world_(&world) {}

    EventTypeId eventTypeOf(EventKey key) const noexcept;
    // Verdadeiro quando há estado ativo: o que o contexto cria nasce com escopo.
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

    // O evento é despachado na rodada seguinte do passo em curso.
    template <class T>
    void emit(const T& event) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view name = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "emit: evento \"%.*s\" não declarado", static_cast<int>(name.size()), name.data());

      emitById(type, &event);
    }
    void emitById(EventTypeId type, const void* event);

    Scheduler& schedule() const noexcept;
  };

  // Mudanças estruturais diretas no World, para quem roda fora de qualquer iteração (handlers e tarefas).
  class DirectContext : public EngineContext {
   protected:
    DirectContext(Engine& engine, ecs::World& world) noexcept
      : EngineContext(engine, world) {}

   public:
    // Com estado ativo a entidade nasce com escopo (some na troca de estado) até receber persist.
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
    // Tira o escopo; a entidade precisa estar viva (OLY_ASSERT) e sem a tag não há efeito.
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
