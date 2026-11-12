#include "oly/app/context.hpp"

#include "oly/app/engine.hpp"

namespace oly::app {
  EventTypeId EngineContext::eventTypeOf(EventKey key) const noexcept {
    return engine_->eventTypeOf(key);
  }

  float EngineContext::fixedDeltaTime() const noexcept {
    return static_cast<float>(engine_->fixedDeltaTime());
  }

  double EngineContext::time() const noexcept {
    return engine_->time();
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
}
