import assert from "node:assert/strict";
import { test } from "node:test";

import { EngineContractError } from "@oly/core";
import type { Block } from "@oly/core";
import type { World } from "@oly/core";
import { Enemy, Mixed, Player, Position, Velocity, loadFixture } from "./fixture.js";

const { runtime, fixture } = await loadFixture();

function withWorld(body: (world: World) => void): void {
  const world = runtime.createWorld();
  try {
    body(world);
    assert.equal(world.checkInvariants(), null);
  } finally {
    world.dispose();
  }
}

function spawnMovers(world: World, count: number): number[] {
  const entities: number[] = [];
  for (let i = 0; i < count; ++i) {
    const types = i % 2 === 0 ? ([Position, Velocity] as const) : ([Position, Velocity, Enemy] as const);
    entities.push(world.spawn(types, { Position: { x: i, y: 0 }, Velocity: { x: 1, y: 2 } }));
  }

  return entities;
}

test("movimento no TS e no C++ dão o mesmo resultado", () => {
  withWorld((tsWorld) => {
    withWorld((cppWorld) => {
      const count = 1000;
      const tsEntities = spawnMovers(tsWorld, count);
      const cppEntities = spawnMovers(cppWorld, count);

      tsWorld.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
        const velocity = block.read(Velocity);
        const position = block.write(Position);
        for (let row = 0; row < block.count; ++row) {
          position.x.set(row, position.x.get(row) + velocity.x.get(row) * 0.5);
          position.y.set(row, position.y.get(row) + velocity.y.get(row) * 0.5);
        }
      });
      fixture.fixtureMove(cppWorld.handle, 0.5);

      let expectedSum = 0;
      for (let i = 0; i < count; ++i) {
        assert.deepEqual(tsWorld.get(tsEntities[i]!, Position), { x: i + 0.5, y: 1 });
        assert.deepEqual(cppWorld.get(cppEntities[i]!, Position), tsWorld.get(tsEntities[i]!, Position));
        expectedSum += i + 0.5;
      }
      assert.equal(fixture.fixtureSumPositionX(cppWorld.handle), expectedSum);
      assert.equal(fixture.fixtureSumPositionX(tsWorld.handle), expectedSum);
    });
  });
});

test("blocos, contagens e filtros", () => {
  withWorld((world) => {
    spawnMovers(world, 1000);

    const count = (spec: Parameters<World["query"]>[0]): { blocks: number; rows: number } => {
      let blocks = 0;
      let rows = 0;
      world.query(spec).eachBlock((block) => {
        ++blocks;
        rows += block.count;
      });

      return { blocks, rows };
    };

    assert.deepEqual(count({ read: [Position] }), { blocks: 2, rows: 1000 });
    assert.deepEqual(count({ read: [Position], with: [Enemy] }), { blocks: 1, rows: 500 });
    assert.deepEqual(count({ read: [Position], without: [Enemy] }), { blocks: 1, rows: 500 });
  });
});

test("entity(row) identifica a linha", () => {
  withWorld((world) => {
    spawnMovers(world, 100);

    world.query({ read: [Position] }).eachBlock((block) => {
      const position = block.read(Position);
      for (let row = 0; row < block.count; ++row) {
        assert.equal(world.get(block.entity(row), Position).x, position.x.get(row));
      }
    });
  });
});

test("query criada antes dos arquétipos os enxerga", () => {
  withWorld((world) => {
    const query = world.query({ read: [Position] });

    world.spawn([Position, Player]);
    world.spawn([Position, Enemy]);

    let rows = 0;
    query.eachBlock((block) => {
      rows += block.count;
    });
    assert.equal(rows, 2);
  });
});

test("tabela esvaziada não gera bloco", () => {
  withWorld((world) => {
    const entity = world.spawn([Position]);
    const query = world.query({ read: [Position] });
    world.destroy(entity);

    let blocks = 0;
    query.eachBlock(() => {
      ++blocks;
    });
    assert.equal(blocks, 0);
  });
});

test("mesma spec devolve a mesma Query", () => {
  withWorld((world) => {
    const spec = { read: [Velocity], write: [Position], without: [Enemy] };

    assert.equal(world.query(spec), world.query({ ...spec }));
    assert.notEqual(world.query(spec), world.query({ read: [Velocity], write: [Position] }));
  });
});

test("campos f64, u8, i16 e entity pela view", () => {
  withWorld((world) => {
    const other = world.spawn([Player]);
    const entity = world.spawn([Mixed], { Mixed: { a: 200, b: 1.5, c: -3, owner: other } });

    world.query({ read: [Mixed] }).eachBlock((block) => {
      const mixed = block.read(Mixed);
      assert.deepEqual(
        { a: mixed.a.get(0), b: mixed.b.get(0), c: mixed.c.get(0), owner: mixed.owner.get(0) },
        world.get(entity, Mixed),
      );
    });
  });
});

test("raw() lê o mesmo que get()", () => {
  withWorld((world) => {
    spawnMovers(world, 50);

    world.query({ read: [Position] }).eachBlock((block) => {
      const position = block.read(Position);
      const { array, base, stride } = position.x.raw();
      for (let row = 0; row < block.count; ++row) {
        assert.equal(array[base + row * stride], position.x.get(row));
      }
    });
  });
});

test("erros de spec e de acesso", () => {
  withWorld((world) => {
    assert.throws(() => world.query({ read: [Player] } as never), EngineContractError);
    assert.throws(() => world.query({ read: [Position], write: [Position] }), EngineContractError);

    world.spawn([Position, Velocity]);
    world.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
      assert.throws(() => block.write(Velocity), EngineContractError);
      assert.throws(() => block.read(Mixed), EngineContractError);
    });
  });
});

test("mudança estrutural dentro de eachBlock é erro e o World segue usável", () => {
  withWorld((world) => {
    const entity = world.spawn([Position]);

    world.query({ read: [Position] }).eachBlock(() => {
      assert.throws(() => world.spawn([Position]), EngineContractError);
      assert.throws(() => world.destroy(entity), EngineContractError);
      assert.throws(() => world.add(entity, Velocity), EngineContractError);
      assert.throws(() => world.remove(entity, Position), EngineContractError);
    });

    assert.ok(world.spawn([Position]));
  });
});

test("exceção no callback encerra a iteração", () => {
  withWorld((world) => {
    world.spawn([Position]);
    const query = world.query({ read: [Position] });

    assert.throws(
      () =>
        query.eachBlock(() => {
          throw new Error("falha do jogo");
        }),
      /falha do jogo/,
    );

    assert.ok(world.spawn([Position]));
  });
});

test("bloco usado fora do callback", () => {
  withWorld((world) => {
    world.spawn([Position]);
    let saved: Block | undefined;

    world.query({ read: [Position] }).eachBlock((block) => {
      saved = block;
    });

    assert.throws(() => saved!.read(Position), EngineContractError);
    assert.throws(() => saved!.entity(0), EngineContractError);
  });
});

test("eachBlock aninhado na mesma query", () => {
  withWorld((world) => {
    for (let i = 0; i < 10; ++i) {
      world.spawn([Position], { Position: { x: i } });
    }
    const query = world.query({ read: [Position] });

    const pairs = new Set<string>();
    query.eachBlock((outer) => {
      const outerPosition = outer.read(Position);
      for (let outerRow = 0; outerRow < outer.count; ++outerRow) {
        query.eachBlock((inner) => {
          const innerPosition = inner.read(Position);
          for (let innerRow = 0; innerRow < inner.count; ++innerRow) {
            pairs.add(`${outerPosition.x.get(outerRow)},${innerPosition.x.get(innerRow)}`);
          }
        });
      }
    });

    assert.equal(pairs.size, 100);
    assert.ok(pairs.has("3,7"));
  });
});

test("crescimento da memória durante eachBlock", () => {
  withWorld((world) => {
    const count = 100;
    for (let i = 0; i < count; ++i) {
      world.spawn([Position]);
    }

    world.query({ write: [Position] }).eachBlock((block) => {
      const position = block.write(Position);
      const before = runtime.module.HEAPU8.buffer;
      const pointer = runtime.bridge.allocate(64 * 1024 * 1024);
      assert.notEqual(runtime.module.HEAPU8.buffer, before);

      for (let row = 0; row < block.count; ++row) {
        position.x.set(row, 42);
      }
      runtime.bridge.free(pointer);
    });

    assert.equal(fixture.fixtureSumPositionX(world.handle), 42 * count);
  });
});

test("dispose dentro de eachBlock é erro e o World segue usável", () => {
  withWorld((world) => {
    world.spawn([Position, Velocity]);
    world.query({ read: [Velocity], write: [Position] }).eachBlock(() => {
      assert.throws(() => world.dispose(), EngineContractError);
    });
    world.spawn([Position]);
  });
});

test("checkInvariants sincroniza o heap da Bridge depois de chamar o C++", () => {
  withWorld((world) => {
    const bridge = runtime.bridge;
    const pointers: number[] = [];
    const before = runtime.module.HEAPU8.buffer;
    while (runtime.module.HEAPU8.buffer === before) {
      pointers.push(runtime.module._malloc(8 * 1024 * 1024));
    }
    assert.notEqual(bridge.heap.buffer, runtime.module.HEAPU8.buffer);

    assert.equal(world.checkInvariants(), null);

    assert.equal(bridge.heap.buffer, runtime.module.HEAPU8.buffer);
    pointers.forEach((pointer) => runtime.module._free(pointer));
  });
});
