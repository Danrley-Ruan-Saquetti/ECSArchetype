#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "oly/app/task.hpp"

namespace oly::app::detail {
  struct DueTask {
    TaskFunction function;
    TaskHandle handle;
    bool lastRun;
    TaskPayload payload;
  };

  // Min-heap por (vencimento, sequência) sobre slots com geração. Cancelar só libera o slot; a entrada do heap fica
  // e é descartada quando chega ao topo. Não conhece a Engine: o vencimento chega pronto de quem agenda.
  class TaskQueue {
   private:
    struct Slot {
      TaskFunction function = nullptr;
      TaskDiscardFunction discard = nullptr;
      TaskPayload payload;
      // O vencimento do every é anchor + periods * interval, não uma soma repetida: o erro de arredondamento da
      // soma cresce e, depois de alguns minutos, o disparo deixaria de alcançar o limite do passo.
      double anchor = 0.0;
      double periods = 0.0;
      double interval = 0.0;
      uint32_t generation = 0;
      bool live = false;
      bool scoped = false;
    };

    struct HeapEntry {
      double due;
      uint64_t sequence;
      uint32_t slot;
      uint32_t generation;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeSlots_;
    std::vector<HeapEntry> heap_;
    // Tarefas criadas durante a execução esperam aqui: com a tolerância do limite, uma nova de vencimento "agora"
    // ficaria no topo à frente de uma antiga vencida e a bloquearia.
    std::vector<HeapEntry> deferred_;
    uint64_t nextSequence_ = 0;
    uint32_t pendingCount_ = 0;
    bool running_ = false;

   public:
    TaskQueue() = default;
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    ~TaskQueue();

    // A fila não valida atraso nem função; isso é do Scheduler. interval 0 é tarefa de uma execução só.
    // Tarefa com escopo é cancelada por cancelScoped (troca de estado) até receber persist.
    TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload, bool scoped = false);
    // Handle padrão e handle antigo (slot reusado) devolvem false. Slot fora do intervalo é erro de contrato.
    // O descarte roda com uma cópia do payload depois de liberar o slot, então pode mexer na fila.
    bool cancel(TaskHandle task);
    bool isPending(TaskHandle task) const;
    // Tira o escopo de uma tarefa pendente; false para handle padrão ou não pendente.
    bool persist(TaskHandle task);
    uint32_t pendingCount() const noexcept {
      return pendingCount_;
    }

    void beginRun() noexcept;
    // Um every volta ao heap com o próximo vencimento sem deriva e sempre depois de limit, então rende uma vez por
    // chamada; um after sai da fila (sem descarte) antes de ser devolvido.
    std::optional<DueTask> popDue(double limit);
    void endRun();
    void recoverFromInterruptedRun();

    // Descarta cada tarefa pendente uma vez, inclusive as agendadas por descartes.
    void discardAll();
    // Cancela, com descarte, cada tarefa com escopo, inclusive as com escopo agendadas por descartes.
    void cancelScoped();

   private:
    bool matches(TaskHandle task, const char* operation) const;
    void release(uint32_t slot);
    double advance(Slot& slot, double limit) const;
    void flushDeferred();
  };
}
