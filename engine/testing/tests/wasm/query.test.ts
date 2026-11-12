import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError } from "@oly/core";
import type { Block, World } from "@oly/core";
import { bridgeOf, moduleOf } from "@oly/core/internal";

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
  for (let index = 0; index < count; ++index) {
    const types = index % 2 === 0 ? ([Position, Velocity] as const) : ([Position, Velocity, Enemy] as const);
    entities.push(world.spawn(types, { Position: { x: index, y: 0 }, Velocity: { x: 1, y: 2 } }));
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
      for (let index = 0; index < count; ++index) {
        assert.deepEqual(tsWorld.get(tsEntities[index]!, Position), { x: index + 0.5, y: 1 });
        assert.deepEqual(cppWorld.get(cppEntities[index]!, Position), tsWorld.get(tsEntities[index]!, Position));
        expectedSum += index + 0.5;
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
    assert.throws(() => world.query({ read: [Player] } as never), ContractError);
    assert.throws(() => world.query({ read: [Position], write: [Position] }), ContractError);

    world.spawn([Position, Velocity]);
    world.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
      assert.throws(() => block.write(Velocity), ContractError);
      assert.throws(() => block.read(Mixed), ContractError);
    });
  });
});

test("mudança estrutural dentro de eachBlock é erro e o World segue usável", () => {
  withWorld((world) => {
    const entity = world.spawn([Position]);

    world.query({ read: [Position] }).eachBlock(() => {
      assert.throws(() => world.spawn([Position]), ContractError);
      assert.throws(() => world.destroy(entity), ContractError);
      assert.throws(() => world.add(entity, Velocity), ContractError);
      assert.throws(() => world.remove(entity, Position), ContractError);
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

    assert.throws(() => saved!.read(Position), ContractError);
    assert.throws(() => saved!.entity(0), ContractError);
  });
});

test("eachBlock aninhado na mesma query", () => {
  withWorld((world) => {
    for (let index = 0; index < 10; ++index) {
      world.spawn([Position], { Position: { x: index } });
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
    for (let index = 0; index < count; ++index) {
      world.spawn([Position]);
    }

    world.query({ write: [Position] }).eachBlock((block) => {
      const position = block.write(Position);
      const before = moduleOf(runtime).HEAPU8.buffer;
      const pointer = bridgeOf(runtime).allocate(64 * 1024 * 1024);
      assert.notEqual(moduleOf(runtime).HEAPU8.buffer, before);

      for (let row = 0; row < block.count; ++row) {
        position.x.set(row, 42);
      }
      bridgeOf(runtime).free(pointer);
    });

    assert.equal(fixture.fixtureSumPositionX(world.handle), 42 * count);
  });
});

test("dispose dentro de eachBlock é erro e o World segue usável", () => {
  withWorld((world) => {
    world.spawn([Position, Velocity]);

    world.query({ read: [Velocity], write: [Position] }).eachBlock(() => {
      assert.throws(() => world.dispose(), ContractError);
    });

    world.spawn([Position]);
  });
});

test("checkInvariants sincroniza o heap da Bridge depois de chamar o C++", () => {
  withWorld((world) => {
    const bridge = bridgeOf(runtime);
    const pointers: number[] = [];
    const before = moduleOf(runtime).HEAPU8.buffer;
    while (moduleOf(runtime).HEAPU8.buffer === before) {
      pointers.push(moduleOf(runtime)._malloc(8 * 1024 * 1024));
    }
    assert.notEqual(bridge.heap.buffer, moduleOf(runtime).HEAPU8.buffer);

    assert.equal(world.checkInvariants(), null);

    assert.equal(bridge.heap.buffer, moduleOf(runtime).HEAPU8.buffer);
    pointers.forEach((pointer) => moduleOf(runtime)._free(pointer));
  });
});

test("single devolve a entidade e cópias dos componentes da única linha", () => {
  withWorld((world) => {
    const player = world.spawn([Position, Velocity, Player], { Position: { x: 1, y: 2 }, Velocity: { x: 3, y: 4 } });
    world.spawn([Position, Enemy]);

    const row = world.query({ read: [Velocity], write: [Position], with: [Player] }).single();
    const copy = row.get(Position);
    (copy as { x: number }).x = 99;

    assert.equal(row.entity, player);
    assert.deepEqual(row.get(Position), { x: 99, y: 2 });
    assert.deepEqual(row.get(Velocity), { x: 3, y: 4 });
    assert.deepEqual(world.get(player, Position), { x: 1, y: 2 });
  });
});

test("single escolhe pela spec entre várias entidades", () => {
  withWorld((world) => {
    world.spawn([Position, Enemy]);
    world.spawn([Position, Enemy]);
    const player = world.spawn([Position, Player]);
    world.spawn([Position, Velocity]);
    world.spawn([Position, Velocity]);

    assert.equal(world.query({ read: [Position], with: [Player] }).single().entity, player);
    assert.equal(world.query({ read: [Position], with: [Player], without: [Enemy] }).trySingle()?.entity, player);
  });
});

test("trySingle devolve null sem linhas e a linha com uma", () => {
  withWorld((world) => {
    const query = world.query({ read: [Position], with: [Player] });
    assert.equal(query.trySingle(), null);

    const player = world.spawn([Position, Player], { Position: { x: 5, y: 6 } });
    const row = query.trySingle();

    assert.equal(row?.entity, player);
    assert.deepEqual(row?.get(Position), { x: 5, y: 6 });
  });
});

test("single dentro do eachBlock de outra query", () => {
  withWorld((world) => {
    const player = world.spawn([Position, Player], { Position: { x: 7, y: 8 } });
    world.spawn([Velocity]);
    const seen: number[] = [];

    world.query({ read: [Velocity] }).eachBlock(() => {
      seen.push(world.query({ read: [Position], with: [Player] }).single().get(Position).x);
    });

    assert.deepEqual(seen, [7]);
    assert.equal(world.isAlive(player), true);
    assert.equal(world.checkInvariants(), null);
  });
});

test("erros de single, trySingle e SingleRow.get", () => {
  withWorld((world) => {
    const query = world.query({ read: [Position] });
    assert.throws(() => query.single(), new ContractError("single: query com 0 linhas; esperava 1"));

    world.spawn([Position]);
    world.spawn([Position]);
    assert.throws(() => query.single(), /single: query com 2 linhas/);
    assert.throws(() => query.trySingle(), /trySingle: query com 2 linhas; esperava 0 ou 1/);
    assert.equal(world.checkInvariants(), null);

    const only = world.query({ read: [Position], with: [Enemy] });
    world.spawn([Position, Enemy]);
    assert.throws(() => only.single().get(Velocity), /get type=Velocity: tipo não está na query/);
  });
});
