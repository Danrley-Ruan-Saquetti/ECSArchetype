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

  void discardAndScheduleScoped(void*) {
    ++discardCount;
    queueToReenter->push(1.0, 0.0, taskC, countDiscard, {}, true);
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

    TaskHandle pushScoped(double due, TaskFunction function) {
      return queue.push(due, 0.0, function, countDiscard, {}, true);
    }
  };
}

TEST_CASE("popDue returns in due-time order and, on ties, in creation order") {
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

TEST_CASE("popDue respects the limit") {
  Fixture fixture;
  fixture.push(1.0, taskA);

  CHECK(!fixture.queue.popDue(0.5));
  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("after leaves the queue before being returned") {
  Fixture fixture;
  fixture.push(1.0, taskA);

  auto task = fixture.queue.popDue(1.0);

  REQUIRE(task);
  CHECK(task->lastRun);
  CHECK(!fixture.queue.isPending(task->handle));
  CHECK(fixture.queue.pendingCount() == 0);
  CHECK(discardCount == 0);
}

TEST_CASE("every goes back to the heap without drift") {
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

TEST_CASE("late every jumps to the first multiple after the limit") {
  Fixture fixture;
  fixture.push(0.1, taskA, 0.1);

  CHECK(fixture.queue.popDue(0.55));
  CHECK(!fixture.queue.popDue(0.55));
  CHECK(fixture.queue.popDue(0.61));
}

TEST_CASE("every with a tiny interval fires once per popDue call") {
  Fixture fixture;
  const double limit = 1.0 / 60.0;
  fixture.push(1e-9, taskA, 1e-9);

  CHECK(fixture.queue.popDue(limit).has_value());
  CHECK(!fixture.queue.popDue(limit).has_value());
  CHECK(fixture.queue.popDue(limit + 1e-8).has_value());
}

TEST_CASE("every with an interval below one ulp of the limit does not repeat in the same round") {
  Fixture fixture;
  const double limit = 3000.0;
  fixture.push(limit, taskA, 1e-13);

  CHECK(fixture.queue.popDue(limit).has_value());
  CHECK(!fixture.queue.popDue(limit).has_value());
  CHECK(fixture.queue.popDue(limit + 1.0).has_value());
}

TEST_CASE("every with a denormal interval does not get the task stuck") {
  Fixture fixture;
  fixture.push(1.0, taskA, 5e-324);

  CHECK(fixture.queue.popDue(1.0).has_value());
  CHECK(!fixture.queue.popDue(1.0).has_value());
  CHECK(fixture.queue.popDue(2.0).has_value());
}

TEST_CASE("cancelled task does not come out of popDue and cancelling again returns false") {
  Fixture fixture;
  const TaskHandle handle = fixture.push(1.0, taskA);

  CHECK(fixture.queue.cancel(handle));
  CHECK(!fixture.queue.cancel(handle));
  CHECK(!fixture.queue.popDue(10.0));
  CHECK(discardCount == 1);
}

TEST_CASE("stale handle does not cancel the task that reused the slot") {
  Fixture fixture;
  const TaskHandle first = fixture.push(1.0, taskA);
  fixture.queue.cancel(first);
  const TaskHandle second = fixture.push(1.0, taskB);

  CHECK(second.slot == first.slot);
  CHECK(!fixture.queue.cancel(first));
  CHECK(!fixture.queue.isPending(first));
  CHECK(fixture.queue.isPending(second));
}

TEST_CASE("default handle: cancel and isPending return false") {
  Fixture fixture;

  CHECK(!fixture.queue.cancel(TaskHandle{}));
  CHECK(!fixture.queue.isPending(TaskHandle{}));
}

TEST_CASE("push during the run only enters the heap at endRun") {
  Fixture fixture;

  fixture.queue.beginRun();
  const TaskHandle created = fixture.push(0.0, taskA);

  CHECK(fixture.queue.isPending(created));
  CHECK(!fixture.queue.popDue(1.0));

  fixture.queue.endRun();

  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("recoverFromInterruptedRun keeps the tasks created before the exception") {
  Fixture fixture;

  fixture.queue.beginRun();
  fixture.push(0.0, taskA);
  fixture.queue.recoverFromInterruptedRun();

  CHECK(fixture.queue.popDue(1.0));
}

TEST_CASE("discard receives the copy of the payload and runs once") {
  Fixture fixture;
  const TaskHandle handle = fixture.queue.push(1.0, 0.0, taskA, recordDiscard, TaskPayload::of(Value{7}));

  fixture.queue.cancel(handle);

  CHECK(discardedValues == std::vector<uint32_t>{7});
  CHECK(discardCount == 1);
}

TEST_CASE("discardAll discards each pending task once, including the one scheduled by a discard") {
  Fixture fixture;
  queueToReenter = &fixture.queue;
  fixture.queue.push(1.0, 0.0, taskA, discardAndSchedule, {});
  fixture.queue.push(2.0, 0.0, taskB, countDiscard, {});

  fixture.queue.discardAll();

  CHECK(discardCount == 3);
  CHECK(fixture.queue.pendingCount() == 0);
  CHECK(!fixture.queue.popDue(10.0));
}

TEST_CASE("destructor discards whatever is pending") {
  Fixture fixture;

  {
    TaskQueue scoped;
    scoped.push(1.0, 0.0, taskA, countDiscard, {});
    scoped.push(2.0, 1.0, taskB, countDiscard, {});
  }

  CHECK(discardCount == 2);
}

TEST_CASE("payloadKey distinguishes types and TaskPayload::of copies the bytes") {
  const TaskPayload payload = TaskPayload::of(Value{42});

  CHECK(oly::app::payloadKey<Value>() != oly::app::payloadKey<Other>());
  CHECK(payload.key == oly::app::payloadKey<Value>());
  CHECK(reinterpret_cast<const Value*>(payload.bytes)->number == 42);
}

TEST_CASE("push is unscoped by default: cancelScoped does not touch it") {
  Fixture fixture;
  const TaskHandle handle = fixture.push(1.0, taskA);

  fixture.queue.cancelScoped();

  CHECK(fixture.queue.isPending(handle));
  CHECK(discardCount == 0);
}

TEST_CASE("cancelScoped cancels only the scoped ones, with discard once") {
  Fixture fixture;
  fixture.pushScoped(1.0, taskA);
  fixture.pushScoped(2.0, taskB);
  const TaskHandle unscoped = fixture.push(3.0, taskC);

  fixture.queue.cancelScoped();

  CHECK(discardCount == 2);
  CHECK(fixture.queue.pendingCount() == 1);
  CHECK(fixture.queue.isPending(unscoped));
}

TEST_CASE("cancelScoped repeats for scoped tasks scheduled by discards") {
  Fixture fixture;
  queueToReenter = &fixture.queue;
  fixture.queue.push(1.0, 0.0, taskA, discardAndScheduleScoped, {}, true);

  fixture.queue.cancelScoped();

  CHECK(discardCount == 2);
  CHECK(fixture.queue.pendingCount() == 0);
}

TEST_CASE("persist removes the scope") {
  Fixture fixture;
  const TaskHandle handle = fixture.pushScoped(1.0, taskA);

  const bool persisted = fixture.queue.persist(handle);
  fixture.queue.cancelScoped();

  CHECK(persisted);
  CHECK(fixture.queue.isPending(handle));
  CHECK(discardCount == 0);
}

TEST_CASE("persist of a non-pending task or of TaskHandle{} returns false") {
  Fixture fixture;
  const TaskHandle canceled = fixture.pushScoped(1.0, taskA);
  fixture.queue.cancel(canceled);

  CHECK(!fixture.queue.persist(canceled));
  CHECK(!fixture.queue.persist(TaskHandle{}));
}

TEST_CASE("cancelScoped does not catch stale entries of a slot reused without scope") {
  Fixture fixture;
  const TaskHandle scoped = fixture.pushScoped(1.0, taskA);
  fixture.queue.cancel(scoped);
  const TaskHandle unscoped = fixture.push(1.0, taskB);

  fixture.queue.cancelScoped();

  CHECK(unscoped.slot == scoped.slot);
  CHECK(fixture.queue.isPending(unscoped));
}
