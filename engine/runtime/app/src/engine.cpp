#include "oly/app/engine.hpp"

#include <cmath>
#include <optional>

#include "oly/app/task_context.hpp"
#include "oly/core/assert.hpp"

namespace oly::app {
  Engine::Engine(ecs::World& world, EngineConfig config)
    : world_(world),
      config_(config),
      fixedDeltaTime_(1.0 / config.fixedHz),
      commands_(world.commands()) {
    OLY_ASSERT(config.fixedHz > 0.0, "Engine: fixedHz=%f must be positive", config.fixedHz);
    OLY_ASSERT(config.maxStepsPerFrame > 0, "Engine: maxStepsPerFrame must be positive");
    OLY_ASSERT(config.maxEventRounds > 0, "Engine: maxEventRounds must be positive");
  }

  SystemId Engine::addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData) {
    OLY_ASSERT(!inFrame_, "addSystem: system \"%.*s\" registered during frame", static_cast<int>(name.size()), name.data());

    const SystemId id = static_cast<SystemId>(systemLocations_.size());
    std::vector<SystemEntry>& entries = phases_[static_cast<uint32_t>(phase)];

    systemLocations_.emplace_back(phase, static_cast<uint32_t>(entries.size()));
    entries.push_back(SystemEntry{function, userData, std::string(name), id, false, 0});

    return id;
  }

  void Engine::setWhilePaused(SystemId system, bool whilePaused) {
    const auto [phase, position] = systemLocations_[system];
    SystemEntry& entry = phases_[static_cast<uint32_t>(phase)][position];
    OLY_ASSERT(isGameplayPhase(phase), "setWhilePaused: render system \"%s\" cannot be whilePaused", entry.name.c_str());

    entry.whilePaused = whilePaused;
  }

  EventTypeId Engine::declareEventType(EventKey key, std::string_view name, uint32_t size) {
    OLY_ASSERT(!inFrame_, "declareEvent: event \"%.*s\" declared during frame", static_cast<int>(name.size()), name.data());
    OLY_ASSERT(queues_.typeOf(key) == InvalidEventType, "declareEvent: event \"%.*s\" already declared", static_cast<int>(name.size()), name.data());

    const EventTypeId type = queues_.declare(key, name, size);
    handlers_.emplace_back();

    return type;
  }

  HandlerId Engine::onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData) {
    OLY_ASSERT(!inFrame_, "on: handler \"%.*s\" registered during frame", static_cast<int>(name.size()), name.data());
    OLY_ASSERT(type < queues_.typeCount(), "on: handler \"%.*s\" for event type %u not declared", static_cast<int>(name.size()), name.data(), type);

    const HandlerId id = static_cast<HandlerId>(handlerLocations_.size());
    handlerLocations_.emplace_back(type, static_cast<uint32_t>(handlers_[type].size()));
    handlers_[type].push_back(HandlerEntry{function, userData, std::string(name), id, 0});

    return id;
  }

  void Engine::emitById(EventTypeId type, const void* event) {
    OLY_ASSERT(type < queues_.typeCount(), "emit: event type %u not declared", type);

    queues_.push(type, event);
  }

  void Engine::setHidden(bool hidden) {
    hidden_ = hidden;
    if (hidden) {
      actions_.releaseAll();
    }

    restartClock();
  }

  void Engine::restartClock() {
    accumulator_ = 0.0;
    hasLastTime_ = false;
  }

  // An exception from a system (e.g. JS unwinding the WASM without destructors) leaves inFrame_ set.
  void Engine::recoverFromInterruptedFrame() {
    commands_ = world_.commands();
    queues_.clear();
    requestedState_ = NoState;
    scheduler_.queue_.recoverFromInterruptedRun();
    inFrame_ = false;
    actions_.setDeclarationsLocked(false);
    restartClock();
  }

  void Engine::frame(double nowSeconds) {
    if (inFrame_) {
      recoverFromInterruptedFrame();
    }

    if (hidden_) {
      return;
    }
    if (!hasLastTime_) {
      lastTime_ = nowSeconds;
      hasLastTime_ = true;
      return;
    }
    OLY_ASSERT(nowSeconds >= lastTime_, "frame: time %f less than the previous %f", nowSeconds, lastTime_);

    inFrame_ = true;
    actions_.setDeclarationsLocked(true);
    accumulator_ += nowSeconds - lastTime_;
    lastTime_ = nowSeconds;

    const double threshold = fixedDeltaTime_ * (1.0 - StepTolerance);
    uint32_t stepsRun = 0;
    while (accumulator_ >= threshold && stepsRun < config_.maxStepsPerFrame) {
      runFixedStep();
      accumulator_ -= fixedDeltaTime_;
      if (accumulator_ < 0.0) {
        accumulator_ = 0.0;
      }
      ++stepsRun;
    }

    // Discarding the steps that did not fit avoids the lag spiral (spec game-engine-design §4.3).
    if (accumulator_ >= threshold) {
      accumulator_ = std::fmod(accumulator_, fixedDeltaTime_);
      if (accumulator_ >= threshold) {
        accumulator_ = 0.0;
      }
    }

    interpolation_ = accumulator_ / fixedDeltaTime_;
    runPhase(Phase::Render);
    inFrame_ = false;
    actions_.setDeclarationsLocked(false);
  }

  void Engine::runFixedStep() {
    // Fixed at the start: pause/resume requested by a system only take effect in the next step.
    pausedThisStep_ = pauseRequested_;
    eventRoundsThisStep_ = 0;

    captureActions();
    if (!pausedThisStep_) {
      runDueSchedule();
    }

    runPhase(Phase::UpdateBefore);
    runPhase(Phase::Update);
    runPhase(Phase::UpdateAfter);

    dispatchEvents();
    applyStateTransition();

    // The clock only advances after dispatch: handlers see the same time() as the step's systems.
    if (!pausedThisStep_) {
      ++steps_;
    }
  }

  void Engine::dispatchEvents() {
    while (queues_.beginRound()) {
      if (eventRoundsThisStep_ == config_.maxEventRounds) {
        OLY_ASSERT(false, "dispatchEvents: limit of %u event rounds exceeded; pending: %s", config_.maxEventRounds, queues_.pendingNames().c_str());

        // Without asserts, discards what is left so the step continues in a defined state.
        queues_.clear();
        return;
      }
      ++eventRoundsThisStep_;

      for (EventTypeId type = 0; type < queues_.typeCount(); ++type) {
        const uint32_t count = queues_.batchCount(type);
        if (count == 0) {
          continue;
        }

        const std::byte* data = queues_.batchData(type);
        for (HandlerEntry& entry : handlers_[type]) {
          if (!runsInState(entry.states)) {
            continue;
          }

          HandlerContext context(*this, world_, type, entry.id, entry.name, entry.userData, data, count);
          entry.function(context);
        }
      }

      queues_.endRound();
    }
  }

  void Engine::runDueSchedule() {
    detail::TaskQueue& queue = scheduler_.queue_;
    const double limit = time() + fixedDeltaTime_ * StepTolerance;

    queue.beginRun();
    while (std::optional<detail::DueTask> due = queue.popDue(limit)) {
      TaskContext context(*this, world_, due->handle, due->lastRun, due->payload);
      due->function(context);
    }

    queue.endRun();
  }

  void Engine::runPhase(Phase phase) {
    for (SystemEntry& entry : phases_[static_cast<uint32_t>(phase)]) {
      if (isGameplayPhase(phase) && entry.whilePaused != pausedThisStep_) {
        continue;
      }

      if (!runsInState(entry.states)) {
        continue;
      }

      SystemContext context(*this, world_, commands_, phase, entry.id, entry.name, entry.userData);
      entry.function(context);
    }

    world_.flush(commands_);
  }
}
