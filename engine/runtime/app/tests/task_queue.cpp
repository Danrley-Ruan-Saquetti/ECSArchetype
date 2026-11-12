#include "oly/app/detail/task_queue.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "oly/app/task.hpp"

using oly::app::TaskContext;
using oly::app::TaskFunction;
using oly::app::TaskHandle;
using oly::app::TaskPayload;
using oly::app::detail::TaskQueue;

namespace {
  struct Value {
    uint32_t number;
  };

  struct Other {
    double number;
  };

  uint32_t discardCount = 0;
  std::vector<uint32_t> discardedValues;
  TaskQueue* queueToReenter = nullptr;

  void taskA(TaskContext&) {}
  void taskB(TaskContext&) {}
  void taskC(TaskContext&) {}

  void countDiscard(void*) {
    ++discardCount;
  }

  void recordDiscard(void* payload) {
    ++discardCount;
    discardedValues.push_back(static_cast<Value*>(payload)->number);
  }

  void discardAndSchedule(void*) {
    ++discardCount;
    queueToReenter->push(1.0, 0.0, taskC, countDiscard, {});
  }

  struct Fixture {
    TaskQueue queue;

    Fixture() {
      discardCount = 0;
      discardedValues.clear();
      queueToReenter = nullptr;
    }

    TaskHandle push(double due, TaskFunction function, double interval = 0.0) {
      return queue.push(due, interval, function, countDiscard, {});
    }
  };
}

TEST_CASE("popDue devolve na ordem de vencimento e, no empate, de criação") {
  Fixture fixture;
  fixture.push(2.0, taskA);
  fixture.push(1.0, taskB);
  fixture.push(1.0, taskC);

  std::vector<TaskFunction> order;
  while (auto task = fixture.queue.popDue(5.0)) {
    order.push_back(task->function);
  }

  CHECK(order == std::vector<TaskFunction>{taskB, taskC, taskA});
}

TEST_CASE("popDue respeita o limite") {
  Fixture fixture;
  fixture.push(1.0, taskA);

  CHECK(!fixture.queue.popDue(0.5));
  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("after sai da fila antes de ser devolvido") {
  Fixture fixture;
  fixture.push(1.0, taskA);

  auto task = fixture.queue.popDue(1.0);

  REQUIRE(task);
  CHECK(task->lastRun);
  CHECK(!fixture.queue.isPending(task->handle));
  CHECK(fixture.queue.pendingCount() == 0);
  CHECK(discardCount == 0);
}

TEST_CASE("every volta ao heap sem deriva") {
  Fixture fixture;
  fixture.push(0.1, taskA, 0.1);

  auto first = fixture.queue.popDue(0.1);
  REQUIRE(first);
  CHECK(!first->lastRun);
  CHECK(fixture.queue.isPending(first->handle));
  CHECK(!fixture.queue.popDue(0.15));

  auto second = fixture.queue.popDue(0.25);
  REQUIRE(second);
  CHECK(!second->lastRun);
  CHECK(!fixture.queue.popDue(0.25));
  CHECK(fixture.queue.popDue(0.31));
}

TEST_CASE("every atrasado pula para o primeiro múltiplo depois do limite") {
  Fixture fixture;
  fixture.push(0.1, taskA, 0.1);

  CHECK(fixture.queue.popDue(0.55));
  CHECK(!fixture.queue.popDue(0.55));
  CHECK(fixture.queue.popDue(0.61));
}

TEST_CASE("every com intervalo minúsculo dispara uma vez por chamada de popDue") {
  Fixture fixture;
  const double limit = 1.0 / 60.0;
  fixture.push(1e-9, taskA, 1e-9);

  CHECK(fixture.queue.popDue(limit).has_value());
  CHECK(!fixture.queue.popDue(limit).has_value());
  CHECK(fixture.queue.popDue(limit + 1e-8).has_value());
}

TEST_CASE("cancelada não sai em popDue e cancelar de novo devolve false") {
  Fixture fixture;
  const TaskHandle handle = fixture.push(1.0, taskA);

  CHECK(fixture.queue.cancel(handle));
  CHECK(!fixture.queue.cancel(handle));
  CHECK(!fixture.queue.popDue(10.0));
  CHECK(discardCount == 1);
}

TEST_CASE("handle antigo não cancela a tarefa que reusou o slot") {
  Fixture fixture;
  const TaskHandle first = fixture.push(1.0, taskA);
  fixture.queue.cancel(first);
  const TaskHandle second = fixture.push(1.0, taskB);

  CHECK(second.slot == first.slot);
  CHECK(!fixture.queue.cancel(first));
  CHECK(!fixture.queue.isPending(first));
  CHECK(fixture.queue.isPending(second));
}

TEST_CASE("handle padrão: cancel e isPending devolvem false") {
  Fixture fixture;

  CHECK(!fixture.queue.cancel(TaskHandle{}));
  CHECK(!fixture.queue.isPending(TaskHandle{}));
}

TEST_CASE("push durante a execução só entra no heap no endRun") {
  Fixture fixture;

  fixture.queue.beginRun();
  const TaskHandle created = fixture.push(0.0, taskA);

  CHECK(fixture.queue.isPending(created));
  CHECK(!fixture.queue.popDue(1.0));

  fixture.queue.endRun();

  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("recoverFromInterruptedRun guarda as tarefas criadas antes da exceção") {
  Fixture fixture;

  fixture.queue.beginRun();
  fixture.push(0.0, taskA);
  fixture.queue.recoverFromInterruptedRun();

  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("descarte recebe a cópia do payload e roda uma vez") {
  Fixture fixture;
  const TaskHandle handle = fixture.queue.push(1.0, 0.0, taskA, recordDiscard, TaskPayload::of(Value{7}));

  fixture.queue.cancel(handle);

  CHECK(discardedValues == std::vector<uint32_t>{7});
  CHECK(discardCount == 1);
}

TEST_CASE("discardAll descarta cada pendente uma vez, inclusive o agendado por um descarte") {
  Fixture fixture;
  queueToReenter = &fixture.queue;
  fixture.queue.push(1.0, 0.0, taskA, discardAndSchedule, {});
  fixture.queue.push(2.0, 0.0, taskB, countDiscard, {});

  fixture.queue.discardAll();

  CHECK(discardCount == 3);
  CHECK(fixture.queue.pendingCount() == 0);
  CHECK(!fixture.queue.popDue(10.0));
}

TEST_CASE("destrutor descarta o que estiver pendente") {
  Fixture fixture;

  {
    TaskQueue scoped;
    scoped.push(1.0, 0.0, taskA, countDiscard, {});
    scoped.push(2.0, 1.0, taskB, countDiscard, {});
  }

  CHECK(discardCount == 2);
}

TEST_CASE("payloadKey distingue tipos e TaskPayload::of copia os bytes") {
  const TaskPayload payload = TaskPayload::of(Value{42});

  CHECK(oly::app::payloadKey<Value>() != oly::app::payloadKey<Other>());
  CHECK(payload.key == oly::app::payloadKey<Value>());
  CHECK(reinterpret_cast<const Value*>(payload.bytes)->number == 42);
}
