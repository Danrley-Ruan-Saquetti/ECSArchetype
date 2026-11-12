#include "oly/app/context.hpp"

#include "oly/app/engine.hpp"

namespace oly::app {
  EventTypeId EngineContext::eventTypeOf(EventKey key) const noexcept {
    return engine_->eventTypeOf(key);
  }

  bool EngineContext::spawnsScoped() const noexcept {
    return engine_->state() != NoState;
  }

  float EngineContext::fixedDeltaTime() const noexcept {
    return static_cast<float>(engine_->fixedDeltaTime());
  }

  double EngineContext::time() const noexcept {
    return engine_->time();
  }

  void EngineContext::requestState(StateId state) {
    engine_->requestState(state);
  }

  StateId EngineContext::state() const noexcept {
    return engine_->state();
  }

  void EngineContext::pause() noexcept {
    engine_->pause();
  }

  void EngineContext::resume() noexcept {
    engine_->resume();
  }

  bool EngineContext::paused() const noexcept {
    return engine_->paused();
  }

  input::ActionMap& EngineContext::actions() const noexcept {
    return engine_->actions();
  }

  void EngineContext::emitById(EventTypeId type, const void* event) {
    engine_->emitById(type, event);
  }

  Scheduler& EngineContext::schedule() const noexcept {
    return engine_->schedule();
  }

  void DirectContext::persist(ecs::Entity entity) {
    world_->remove<detail::StateScoped>(entity);
  }
}
