#pragma once

#include <cstdint>

#include "oly/app/detail/task_queue.hpp"
#include "oly/app/task.hpp"

namespace oly::app {
  class Engine;

  // Tarefas no relógio do jogo (Engine::time()). Vencimento = time() + atraso: dentro de um passo, time() é o início
  // do passo; fora de frame (e na fase render), o início do próximo. Tarefas vencidas rodam no passo 2 do passo fixo,
  // só em passo não pausado, na ordem de vencimento e, no empate, de criação. Uma tarefa criada durante o passo 2
  // só roda a partir do passo seguinte. Com um estado ativo a tarefa nasce com escopo: a troca de estado a cancela
  // (com descarte) a menos que receba persist.
  class Scheduler {
   private:
    friend class Engine;

    Engine& engine_;
    detail::TaskQueue queue_;

   public:
    explicit Scheduler(Engine& engine) noexcept
      : engine_(engine) {}
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Base dos templates e da API C. intervalSeconds 0 agenda um after; maior que 0, um every com o primeiro disparo
    // em time() + delaySeconds. Atraso e intervalo precisam ser finitos e não negativos, e a função não pode ser
    // nula (OLY_ASSERT). O descarte, se houver, roda uma vez com o payload quando a tarefa sai da fila sem a última
    // execução (cancel, destruição da Engine); nunca depois do último disparo de um after.
    TaskHandle schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr);

    TaskHandle after(double delaySeconds, TaskFunction function) {
      return schedule(delaySeconds, 0.0, function);
    }
    template <class P>
    TaskHandle after(double delaySeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr) {
      return schedule(delaySeconds, 0.0, function, TaskPayload::of(payload), discard);
    }

    // O intervalo precisa ser positivo e finito (OLY_ASSERT). O próximo vencimento não deriva e cada tarefa dispara
    // no máximo uma vez por passo.
    TaskHandle every(double intervalSeconds, TaskFunction function) {
      requirePositiveInterval(intervalSeconds);

      return schedule(intervalSeconds, intervalSeconds, function);
    }
    template <class P>
    TaskHandle every(double intervalSeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr) {
      requirePositiveInterval(intervalSeconds);

      return schedule(intervalSeconds, intervalSeconds, function, TaskPayload::of(payload), discard);
    }

    // false para tarefa já terminada ou cancelada e para TaskHandle{} (nenhuma tarefa); um after que cancela a si
    // mesmo já saiu da fila. Slot fora do intervalo é erro de contrato.
    bool cancel(TaskHandle task) {
      return queue_.cancel(task);
    }
    bool isPending(TaskHandle task) const {
      return queue_.isPending(task);
    }
    // Tira o escopo de uma tarefa pendente; false se não está pendente.
    bool persist(TaskHandle task) {
      return queue_.persist(task);
    }
    uint32_t pendingCount() const noexcept {
      return queue_.pendingCount();
    }

   private:
    static void requirePositiveInterval(double intervalSeconds);
  };
}
