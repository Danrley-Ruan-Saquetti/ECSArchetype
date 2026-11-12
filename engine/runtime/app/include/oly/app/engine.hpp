#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
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
#include "oly/app/state.hpp"
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
    // Floating-point error in the accumulator must neither lose nor duplicate a step.
    static constexpr double StepTolerance = 1e-9;

    struct SystemEntry {
      SystemFunction function;
      void* userData;
      std::string name;
      SystemId id;
      bool whilePaused;
      StateMask states;
    };

    struct HandlerEntry {
      HandlerFunction function;
      void* userData;
      std::string name;
      HandlerId id;
      StateMask states;
    };

    ecs::World& world_;
    EngineConfig config_;
    double fixedDeltaTime_;

    std::array<std::vector<SystemEntry>, PhaseCount> phases_;
    std::vector<std::pair<Phase, uint32_t>> systemLocations_; // [SystemId]: (phase, position in the phase)
    ecs::CommandBuffer commands_;

    detail::EventQueues queues_;
    std::vector<std::vector<HandlerEntry>> handlers_; // [EventTypeId]
    std::vector<std::pair<EventTypeId, uint32_t>> handlerLocations_; // [HandlerId]: (type, position in the type)
    uint32_t eventRoundsThisStep_ = 0;

    input::ActionMap actions_;
    Scheduler scheduler_{*this};

    struct StateHookEntry {
      StateHookFunction function;
      void* userData;
      std::string name;
      StateHookId id;
    };

    struct StateEntry {
      std::string name;
      std::vector<StateHookEntry> enterHooks;
      std::vector<StateHookEntry> exitHooks;
    };

    std::vector<StateEntry> states_; // [StateId]
    StateHookId stateHookCount_ = 0;
    StateId state_ = NoState;
    StateId requestedState_ = NoState;
    ecs::QueryById* scopedQuery_ = nullptr; // created on the first cleanup: creating it in the constructor would change the World's query count
    std::vector<ecs::Entity> scopedEntities_;

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
    // fixedHz, maxStepsPerFrame and maxEventRounds must be positive (OLY_ASSERT). Two Engines over the same World
    // share the scope tag: a state change in one destroys the scoped entities of the other.
    explicit Engine(ecs::World& world, EngineConfig config = {});
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    ecs::World& world() noexcept {
      return world_;
    }

    // Cannot be called during frame (OLY_ASSERT). Within a phase, systems run in registration order.
    SystemId addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData = nullptr);
    // In the gameplay phases, a whilePaused system runs only in paused steps and the others only in unpaused ones.
    // Render does not accept whilePaused (OLY_ASSERT).
    void setWhilePaused(SystemId system, bool whilePaused);

    // Declaration order is the dispatch order of the types. Cannot be called during frame or repeated for the
    // same type (OLY_ASSERT). The name appears in the error messages.
    template <class T>
    EventTypeId declareEvent(std::string_view name = oly::core::typeName<T>()) {
      static_assert(std::is_trivially_copyable_v<T>, "declareEvent: the event must be trivially copyable");
      static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "declareEvent: event alignment larger than the queue's");

      return declareEventType(eventKey<T>(), name, static_cast<uint32_t>(sizeof(T)));
    }
    // InvalidEventType if the type was not declared in this Engine.
    EventTypeId eventTypeOf(EventKey key) const noexcept {
      return queues_.typeOf(key);
    }
    uint32_t eventTypeCount() const noexcept {
      return queues_.typeCount();
    }

    // The type must be declared and the call cannot happen during frame (OLY_ASSERT). Handlers of the same type run in
    // registration order, once per round, with the whole batch.
    template <class T>
    HandlerId on(std::string_view name, HandlerFunction function, void* userData = nullptr) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view typeName = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "on: handler \"%.*s\" for event \"%.*s\" not declared", static_cast<int>(name.size()), name.data(), static_cast<int>(typeName.size()), typeName.data());

      return onById(type, name, function, userData);
    }
    HandlerId onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData = nullptr);

    // Valid inside and outside frame; outside, the event is dispatched in the next fixed step.
    template <class T>
    void emit(const T& event) {
      const EventTypeId type = eventTypeOf(eventKey<T>());
      [[maybe_unused]] constexpr std::string_view typeName = oly::core::typeName<T>();
      OLY_ASSERT(type != InvalidEventType, "emit: event \"%.*s\" not declared", static_cast<int>(typeName.size()), typeName.data());

      emitById(type, &event);
    }
    void emitById(EventTypeId type, const void* event);

    // nowSeconds cannot go backwards (OLY_ASSERT). Runs up to maxStepsPerFrame fixed steps and then the Render phase
    // once; the whole steps that did not fit are discarded and only the fraction stays accumulated. The first frame only
    // records the time. If the previous frame was interrupted by an exception, this one discards the pending commands, events
    // and state request (including those emitted after the exception) and also only records the time; scheduled tasks stay.
    void frame(double nowSeconds);
    // While hidden, frame does nothing; on showing, the next frame only records the time. Does not touch the game pause.
    // Hiding releases all buttons (the player may release them while the tab is out of focus).
    void setHidden(bool hidden);

    // Declarations are locked during frame; bindings and host input are not.
    input::ActionMap& actions() noexcept {
      return actions_;
    }
    const input::ActionMap& actions() const noexcept {
      return actions_;
    }

    // Flat states: declaration order gives the StateId (dense, at most MaxStates). Cannot be called during
    // frame, repeat a name or exceed the maximum (OLY_ASSERT).
    StateId declareState(std::string_view name);
    uint32_t stateCount() const noexcept {
      return static_cast<uint32_t>(states_.size());
    }
    // Valid inside and outside frame; the last request before stage 5 wins and the change happens there, paused or not.
    // A request made in a state hook takes effect in the next step.
    void requestState(StateId state);
    // NoState until the first change. Only changes in stage 5.
    StateId state() const noexcept {
      return state_;
    }
    // On a change, the onExit hooks of the leaving state run, then the scope cleanup and the onEnter hooks of the entering one, each group in
    // registration order. A hook may emit events, schedule tasks and make direct structural changes, but not
    // register anything. Hooks cannot be registered during frame (OLY_ASSERT). StateHookId counts enter and exit together.
    StateHookId onEnter(StateId state, std::string_view name, StateHookFunction function, void* userData = nullptr);
    StateHookId onExit(StateId state, std::string_view name, StateHookFunction function, void* userData = nullptr);
    // Restricts the system or handler to the given states, in all phases, including render; an empty list or mask
    // removes the restriction. With no active state, restricted ones do not run. These cannot be called during frame
    // (OLY_ASSERT).
    void setSystemStates(SystemId system, std::initializer_list<StateId> states);
    void setSystemStateMask(SystemId system, StateMask mask);
    void setHandlerStates(HandlerId handler, std::initializer_list<StateId> states);
    void setHandlerStateMask(HandlerId handler, StateMask mask);

    // Valid inside and outside frame; tasks survive exception recovery.
    Scheduler& schedule() noexcept {
      return scheduler_;
    }
    const Scheduler& schedule() const noexcept {
      return scheduler_;
    }

    // Requests in the middle of a step take effect from the next fixed step on.
    void pause() noexcept {
      pauseRequested_ = true;
    }
    void resume() noexcept {
      pauseRequested_ = false;
    }
    bool paused() const noexcept {
      return pauseRequested_;
    }

    // Inside a system, it is the start of the current step; it does not advance in paused steps.
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

    void checkState(const char* operation, StateId state) const;
    void checkStateMask(const char* operation, StateMask mask) const;
    StateMask maskOf(const char* operation, std::initializer_list<StateId> states) const;
    StateHookId addStateHook(const char* operation, bool entering, StateId state, std::string_view name, StateHookFunction function, void* userData);
    void runStateHooks(const std::vector<StateHookEntry>& hooks, StateId from, StateId to, bool entering);
    void clearStateScope();

    bool runsInState(StateMask states) const noexcept {
      return states == 0 || (state_ != NoState && ((states >> state_) & 1u) != 0);
    }

    // Stages of the fixed step defined in the spec (2026-11-09-game-engine-design.md §4.3).
    void captureActions() {
      actions_.capture();
    }
    void applyStateTransition();
  };
}
