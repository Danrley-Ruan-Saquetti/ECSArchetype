#include "oly/app/engine.hpp"

#include "oly/app/state_context.hpp"
#include "oly/core/assert.hpp"

namespace oly::app {
  StateId Engine::declareState(std::string_view name) {
    OLY_ASSERT(!inFrame_, "declareState: estado \"%.*s\" declarado durante frame", static_cast<int>(name.size()), name.data());
    OLY_ASSERT(states_.size() < MaxStates, "declareState: estado \"%.*s\" passa do máximo de %u", static_cast<int>(name.size()), name.data(), MaxStates);

    for ([[maybe_unused]] const StateEntry& declared : states_) {
      OLY_ASSERT(declared.name != name, "declareState: estado \"%.*s\" já declarado", static_cast<int>(name.size()), name.data());
    }

    states_.push_back(StateEntry{std::string(name), {}, {}});

    return static_cast<StateId>(states_.size() - 1);
  }

  void Engine::requestState(StateId state) {
    checkState("requestState", state);

    requestedState_ = state;
  }

  StateHookId Engine::onEnter(StateId state, std::string_view name, StateHookFunction function, void* userData) {
    return addStateHook("onEnter", true, state, name, function, userData);
  }

  StateHookId Engine::onExit(StateId state, std::string_view name, StateHookFunction function, void* userData) {
    return addStateHook("onExit", false, state, name, function, userData);
  }

  void Engine::setSystemStates(SystemId system, std::initializer_list<StateId> states) {
    setSystemStateMask(system, maskOf("setSystemStates", states));
  }

  void Engine::setSystemStateMask(SystemId system, StateMask mask) {
    OLY_ASSERT(!inFrame_, "setSystemStates: restrição de estados durante frame");
    checkStateMask("setSystemStates", mask);

    const auto [phase, position] = systemLocations_[system];
    phases_[static_cast<uint32_t>(phase)][position].states = mask;
  }

  void Engine::setHandlerStates(HandlerId handler, std::initializer_list<StateId> states) {
    setHandlerStateMask(handler, maskOf("setHandlerStates", states));
  }

  void Engine::setHandlerStateMask(HandlerId handler, StateMask mask) {
    OLY_ASSERT(!inFrame_, "setHandlerStates: restrição de estados durante frame");
    checkStateMask("setHandlerStates", mask);

    const auto [type, position] = handlerLocations_[handler];
    handlers_[type][position].states = mask;
  }

  void Engine::applyStateTransition() {
    if (requestedState_ == NoState) {
      return;
    }

    const StateId target = requestedState_;
    const StateId from = state_;
    requestedState_ = NoState;

    if (from != NoState) {
      runStateHooks(states_[from].exitHooks, from, target, false);
    }

    state_ = target;
    runStateHooks(states_[target].enterHooks, from, target, true);

    dispatchEvents();
  }

  void Engine::checkState([[maybe_unused]] const char* operation, [[maybe_unused]] StateId state) const {
    OLY_ASSERT(state < states_.size(), "%s: state=%u fora do intervalo (%u estados)", operation, state, static_cast<uint32_t>(states_.size()));
  }

  void Engine::checkStateMask([[maybe_unused]] const char* operation, [[maybe_unused]] StateMask mask) const {
    OLY_ASSERT(states_.size() == MaxStates || (mask >> states_.size()) == 0, "%s: máscara 0x%x com estado não declarado (%u estados)", operation, mask, static_cast<uint32_t>(states_.size()));
  }

  StateMask Engine::maskOf(const char* operation, std::initializer_list<StateId> states) const {
    StateMask mask = 0;
    for (const StateId state : states) {
      checkState(operation, state);

      mask |= StateMask{1} << state;
    }

    return mask;
  }

  StateHookId Engine::addStateHook(const char* operation, bool entering, StateId state, std::string_view name, StateHookFunction function, void* userData) {
    OLY_ASSERT(!inFrame_, "%s: gancho \"%.*s\" registrado durante frame", operation, static_cast<int>(name.size()), name.data());
    checkState(operation, state);

    const StateHookId id = stateHookCount_++;
    StateEntry& entry = states_[state];
    (entering ? entry.enterHooks : entry.exitHooks).push_back(StateHookEntry{function, userData, std::string(name), id});

    return id;
  }

  void Engine::runStateHooks(const std::vector<StateHookEntry>& hooks, StateId from, StateId to, bool entering) {
    for (const StateHookEntry& hook : hooks) {
      StateContext context(*this, world_, hook.id, hook.name, hook.userData, from, to, entering);
      hook.function(context);
    }
  }
}
