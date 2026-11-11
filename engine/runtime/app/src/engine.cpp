#include "oly/app/engine.hpp"

#include <cmath>

#include "oly/core/assert.hpp"

namespace oly::app {
  namespace {
    // Erro de ponto flutuante no acumulador não pode perder nem duplicar um passo.
    constexpr double StepTolerance = 1e-9;
  }

  Engine::Engine(ecs::World& world, EngineConfig config)
    : world_(world),
      config_(config),
      fixedDeltaTime_(1.0 / config.fixedHz),
      commands_(world.commands()) {
    OLY_ASSERT(config.fixedHz > 0.0, "Engine: fixedHz=%f deve ser positivo", config.fixedHz);
    OLY_ASSERT(config.maxStepsPerFrame > 0, "Engine: maxStepsPerFrame deve ser positivo");
    OLY_ASSERT(config.maxEventRounds > 0, "Engine: maxEventRounds deve ser positivo");
  }

  SystemId Engine::addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData) {
    OLY_ASSERT(!inFrame_, "addSystem: sistema \"%.*s\" registrado durante frame", static_cast<int>(name.size()), name.data());

    const SystemId id = static_cast<SystemId>(systemLocations_.size());
    std::vector<SystemEntry>& entries = phases_[static_cast<uint32_t>(phase)];

    systemLocations_.emplace_back(phase, static_cast<uint32_t>(entries.size()));
    entries.push_back(SystemEntry{function, userData, std::string(name), id, false});

    return id;
  }

  void Engine::setWhilePaused(SystemId system, bool whilePaused) {
    const auto [phase, position] = systemLocations_[system];
    SystemEntry& entry = phases_[static_cast<uint32_t>(phase)][position];
    OLY_ASSERT(isGameplayPhase(phase), "setWhilePaused: sistema \"%s\" de render não pode ter whilePaused", entry.name.c_str());

    entry.whilePaused = whilePaused;
  }

  EventTypeId Engine::declareEventType(EventKey key, std::string_view name, uint32_t size) {
    OLY_ASSERT(!inFrame_, "declareEvent: evento \"%.*s\" declarado durante frame", static_cast<int>(name.size()), name.data());
    OLY_ASSERT(queues_.typeOf(key) == InvalidEventType, "declareEvent: evento \"%.*s\" já declarado", static_cast<int>(name.size()), name.data());

    const EventTypeId type = queues_.declare(key, name, size);
    handlers_.emplace_back();

    return type;
  }

  HandlerId Engine::onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData) {
    OLY_ASSERT(!inFrame_, "on: handler \"%.*s\" registrado durante frame", static_cast<int>(name.size()), name.data());
    OLY_ASSERT(type < queues_.typeCount(), "on: handler \"%.*s\" para o tipo de evento %u não declarado", static_cast<int>(name.size()), name.data(), type);

    const HandlerId id = handlerCount_++;
    handlers_[type].push_back(HandlerEntry{function, userData, std::string(name), id});

    return id;
  }

  void Engine::emitById(EventTypeId type, const void* event) {
    OLY_ASSERT(type < queues_.typeCount(), "emit: tipo de evento %u não declarado", type);

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

  // Uma exceção de sistema (ex.: JS desenrolando o WASM sem destrutores) deixa inFrame_ ligado.
  void Engine::recoverFromInterruptedFrame() {
    commands_ = world_.commands();
    queues_.clear();
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
    OLY_ASSERT(nowSeconds >= lastTime_, "frame: tempo %f menor que o anterior %f", nowSeconds, lastTime_);

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

    // Descartar os passos que não couberam evita a espiral de atraso (spec game-engine-design §4.3).
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
    // Fixada no início: pause/resume pedidos por um sistema só valem no próximo passo.
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

    // O relógio só avança depois do despacho: os handlers veem o mesmo time() dos sistemas do passo.
    if (!pausedThisStep_) {
      ++steps_;
    }
  }

  void Engine::dispatchEvents() {
    while (queues_.beginRound()) {
      if (eventRoundsThisStep_ == config_.maxEventRounds) {
        OLY_ASSERT(false, "dispatchEvents: limite de %u rodadas de eventos estourado; pendentes: %s", config_.maxEventRounds, queues_.pendingNames().c_str());

        // Sem asserts, descarta o que sobrou para o passo seguir com estado definido.
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
          HandlerContext context(*this, world_, type, entry.id, entry.name, entry.userData, data, count);
          entry.function(context);
        }
      }

      queues_.endRound();
    }
  }

  void Engine::runPhase(Phase phase) {
    for (SystemEntry& entry : phases_[static_cast<uint32_t>(phase)]) {
      if (isGameplayPhase(phase) && entry.whilePaused != pausedThisStep_) {
        continue;
      }

      SystemContext context(*this, world_, commands_, phase, entry.id, entry.name, entry.userData);
      entry.function(context);
    }

    world_.flush(commands_);
  }
}
