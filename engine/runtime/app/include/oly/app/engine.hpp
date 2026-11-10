#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "oly/app/phase.hpp"
#include "oly/app/system.hpp"
#include "oly/ecs/world.hpp"

namespace oly::app {
  struct EngineConfig {
    double fixedHz = 60.0;
    uint32_t maxStepsPerFrame = 5;
  };

  class Engine {
   private:
    struct SystemEntry {
      SystemFunction function;
      void* userData;
      std::string name;
      SystemId id;
      bool whilePaused;
    };

    ecs::World& world_;
    EngineConfig config_;
    double fixedDeltaTime_;
    std::array<std::vector<SystemEntry>, PhaseCount> phases_;
    std::vector<std::pair<Phase, uint32_t>> systemLocations_; // [SystemId]: (fase, posição na fase)
    ecs::CommandBuffer commands_;
    double accumulator_ = 0.0;
    double lastTime_ = 0.0;
    bool hasLastTime_ = false;
    uint64_t steps_ = 0;
    double interpolation_ = 0.0;
    bool pauseRequested_ = false;
    bool pausedThisStep_ = false;
    bool inFrame_ = false;

   public:
    explicit Engine(ecs::World& world, EngineConfig config = {});
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    ecs::World& world() noexcept {
      return world_;
    }

    SystemId addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData = nullptr);
    void setWhilePaused(SystemId system, bool whilePaused);

    void frame(double nowSeconds);

    void pause() noexcept {
      pauseRequested_ = true;
    }
    void resume() noexcept {
      pauseRequested_ = false;
    }
    bool paused() const noexcept {
      return pauseRequested_;
    }
    double time() const noexcept {
      return static_cast<double>(steps_) * fixedDeltaTime_;
    }
    double fixedDeltaTime() const noexcept {
      return fixedDeltaTime_;
    }
    double interpolation() const noexcept {
      return interpolation_;
    }
    bool inFrame() const noexcept {
      return inFrame_;
    }

   private:
    void runFixedStep();
    void runPhase(Phase phase);
    void captureActions() {}
    void runDueSchedule() {}
    void dispatchEvents() {}
    void applyStateTransition() {}
  };
}
