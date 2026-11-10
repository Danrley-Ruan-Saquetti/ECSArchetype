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

  void Engine::setHidden(bool hidden) {
    hidden_ = hidden;
    restartClock();
  }

  void Engine::restartClock() {
    accumulator_ = 0.0;
    hasLastTime_ = false;
  }

  // Uma exceção de sistema (ex.: JS desenrolando o WASM sem destrutores) deixa inFrame_ ligado.
  void Engine::recoverFromInterruptedFrame() {
    commands_ = world_.commands();
    inFrame_ = false;
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
  }

  void Engine::runFixedStep() {
    // Fixada no início: pause/resume pedidos por um sistema só valem no próximo passo.
    pausedThisStep_ = pauseRequested_;

    captureActions();
    if (!pausedThisStep_) {
      runDueSchedule();
    }

    runPhase(Phase::UpdateBefore);
    runPhase(Phase::Update);
    runPhase(Phase::UpdateAfter);

    if (!pausedThisStep_) {
      ++steps_;
    }

    dispatchEvents();
    applyStateTransition();
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
