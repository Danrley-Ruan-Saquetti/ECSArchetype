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
    bool hidden_ = false;

   public:
    // fixedHz e maxStepsPerFrame precisam ser positivos (OLY_ASSERT).
    explicit Engine(ecs::World& world, EngineConfig config = {});
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    ecs::World& world() noexcept {
      return world_;
    }

    // Não pode ser chamado durante frame (OLY_ASSERT). Dentro da fase, os sistemas rodam na ordem de registro.
    SystemId addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData = nullptr);
    // Nas fases de gameplay, um sistema whilePaused roda só nos passos pausados e os demais só nos não pausados.
    // Render não aceita whilePaused (OLY_ASSERT).
    void setWhilePaused(SystemId system, bool whilePaused);

    // nowSeconds não pode recuar (OLY_ASSERT). Roda até maxStepsPerFrame passos fixos e depois a fase Render uma
    // vez; os passos inteiros que não couberam são descartados e só a fração fica acumulada. O primeiro frame só
    // registra o tempo. Se o frame anterior foi interrompido por exceção, este descarta os comandos pendentes e
    // também só registra o tempo.
    void frame(double nowSeconds);
    // Oculto, frame não faz nada; ao mostrar, o próximo frame só registra o tempo. Não mexe na pausa do jogo.
    void setHidden(bool hidden);

    // Pedidos no meio de um passo valem a partir do próximo passo fixo.
    void pause() noexcept {
      pauseRequested_ = true;
    }
    void resume() noexcept {
      pauseRequested_ = false;
    }
    bool paused() const noexcept {
      return pauseRequested_;
    }

    // Dentro de um sistema, é o início do passo atual; não avança em passos pausados.
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
    void restartClock();
    void recoverFromInterruptedFrame();
    void runFixedStep();
    void runPhase(Phase phase);

    // Etapas do passo fixo previstas na spec (2026-11-09-game-engine-design.md §4.3); vazias até existirem
    // ações de input, schedule, eventos e estados de jogo.
    void captureActions() {}
    void runDueSchedule() {}
    void dispatchEvents() {}
    void applyStateTransition() {}
  };
}
