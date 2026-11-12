#pragma once

#include <string_view>

#include "oly/app/context.hpp"
#include "oly/app/task.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"

namespace oly::app {
  class Engine;

  // Válido só durante a chamada da tarefa. Roda no passo 2, fora de qualquer iteração, com mudanças estruturais
  // diretas como as do HandlerContext.
  class TaskContext : public DirectContext {
   private:
    friend class Engine;

    TaskHandle handle_;
    bool lastRun_;
    TaskPayload payload_;

    TaskContext(Engine& engine, ecs::World& world, TaskHandle handle, bool lastRun, const TaskPayload& payload) noexcept
      : DirectContext(engine, world),
        handle_(handle),
        lastRun_(lastRun),
        payload_(payload) {}

   public:
    TaskHandle handle() const noexcept {
      return handle_;
    }
    // Verdadeiro no disparo de um after: a tarefa não roda mais e nenhum descarte virá, então o payload é liberado aqui.
    bool lastRun() const noexcept {
      return lastRun_;
    }

    template <class P>
    const P& payload() const {
      [[maybe_unused]] constexpr std::string_view requested = oly::core::typeName<P>();
      [[maybe_unused]] const std::string_view scheduled = payload_.typeName.empty() ? std::string_view("(nenhum)") : payload_.typeName;
      OLY_ASSERT(payload_.key == payloadKey<P>(), "payload: tipo \"%.*s\" diferente do agendado \"%.*s\"", static_cast<int>(requested.size()), requested.data(), static_cast<int>(scheduled.size()), scheduled.data());

      return *reinterpret_cast<const P*>(payload_.bytes);
    }
  };
}
