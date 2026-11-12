#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "oly/app/detail/event_queues.hpp"
#include "oly/app/event.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/phase.hpp"
#include "oly/app/scheduler.hpp"
#include "oly/app/system.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"
#include "oly/ecs/world.hpp"
#include "oly/input/action_map.hpp"

namespace oly::app {
  struct EngineConfig {
    double fixedHz = 60.0;
    uint32_t maxStepsPerFrame = 5;
    uint32_t maxEventRounds = 16;
  };

  class Engine {
   private:
    // Erro de ponto flutuante no acumulador não pode perder nem duplicar um passo.
    static constexpr double StepTolerance = 1e-9;

    struct SystemEntry {
      SystemFunction function;
      void* userData;
      std::string name;
      SystemId id;
      bool whilePaused;
    };

    struct HandlerEntry {
      HandlerFunction function;
      void* userData;
      std::string name;
      HandlerId id;
    };

    ecs::World& world_;
    EngineConfig config_;
    double fixedDeltaTime_;

    std::array<std::vector<SystemEntry>, PhaseCount> phases_;
    std::vector<std::pair<Phase, uint32_t>> systemLocations_; // [SystemId]: (fase, posição na fase)
    ecs::CommandBuffer commands_;

    detail::EventQueues queues_;
    std::vector<std::vector<HandlerEntry>> handlers_; // [EventTypeId]
    HandlerId handlerCount_ = 0;
    uint32_t eventRoundsThisStep_ = 0;

    input::ActionMap actions_;
    Scheduler scheduler_{*this};

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
    // fixedHz, maxStepsPerFrame e maxEventRounds precisam ser positivos (OLY_ASSERT).
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

    // A ordem de declaração é a ordem de despacho dos tipos. Não pode ser chamado durante frame nem repetido para o
    // mesmo tipo (OLY_ASSERT). O nome aparece nas mensagens de erro.
    template <class T>
    EventTypeId declareEvent(std::string_view name = oly::core::typeName<T>()) {
      static_assert(std::is_trivially_copyable_v<T>, "declareEvent: o evento precisa ser trivialmente copiável");
      static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "declareEvent: alinhamento do evento maior que o da fila");

      return declareEventType(eventKey<T>(), name, static_cast<uint32_t>(sizeof(T)));
    }
    // InvalidEventType se o tipo não foi declarado nesta Engine.
    EventTypeId eventTypeOf(EventKey key) const noexcept {
      return queues_.typeOf(key);
    }
    uint32_t eventTypeCount() const noexcept {
      return queues_.typeCount();
    }

    // O tipo precisa estar declarado e não pode ser durante frame (OLY_ASSERT). Handlers do mesmo tipo rodam na ordem
    // de registro, uma vez por rodada, com o lote inteiro.
    template <class T>
    HandlerId on(std::string_view name, HandlerFunction function, void* userData = nullptr) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view typeName = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "on: handler \"%.*s\" para o evento \"%.*s\" não declarado", static_cast<int>(name.size()), name.data(), static_cast<int>(typeName.size()), typeName.data());

      return onById(type, name, function, userData);
    }
    HandlerId onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData = nullptr);

    // Vale dentro e fora de frame; fora, o evento é despachado no próximo passo fixo.
    template <class T>
    void emit(const T& event) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view typeName = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "emit: evento \"%.*s\" não declarado", static_cast<int>(typeName.size()), typeName.data());

      emitById(type, &event);
    }
    void emitById(EventTypeId type, const void* event);

    // nowSeconds não pode recuar (OLY_ASSERT). Roda até maxStepsPerFrame passos fixos e depois a fase Render uma
    // vez; os passos inteiros que não couberam são descartados e só a fração fica acumulada. O primeiro frame só
    // registra o tempo. Se o frame anterior foi interrompido por exceção, este descarta os comandos e os eventos
    // pendentes (inclusive os emitidos depois da exceção) e também só registra o tempo; as tarefas agendadas ficam.
    void frame(double nowSeconds);
    // Oculto, frame não faz nada; ao mostrar, o próximo frame só registra o tempo. Não mexe na pausa do jogo.
    // Ocultar solta todos os botões (o jogador pode soltá-los com a aba fora de foco).
    void setHidden(bool hidden);

    // Declarações ficam travadas durante frame; ligações e entrada do host não.
    input::ActionMap& actions() noexcept {
      return actions_;
    }
    const input::ActionMap& actions() const noexcept {
      return actions_;
    }

    // Vale dentro e fora de frame; as tarefas sobrevivem à recuperação de exceção.
    Scheduler& schedule() noexcept {
      return scheduler_;
    }
    const Scheduler& schedule() const noexcept {
      return scheduler_;
    }

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
    EventTypeId declareEventType(EventKey key, std::string_view name, uint32_t size);
    void restartClock();
    void recoverFromInterruptedFrame();
    void runFixedStep();
    void runPhase(Phase phase);
    void dispatchEvents();
    void runDueSchedule();

    // Etapas do passo fixo previstas na spec (2026-11-09-game-engine-design.md §4.3); a vazia espera os estados
    // de jogo.
    void captureActions() {
      actions_.capture();
    }
    void applyStateTransition() {}
  };
}
