# Mapa do repositório

```
CMakeLists.txt  CMakePresets.json  Makefile         build C++ e atalhos
Dockerfile  Dockerfile.wasm  docker-compose.yaml    toolchains (gcc:14 e emsdk 3.1.74)
package.json  tsconfig.base.json  tsconfig.json     npm workspaces e project references
cmake/            funções de build: OlyModule, OlyGame, OlyOptions, OlyWarnings, OlyWasm, Dependencies
engine/
  runtime/        C++, um módulo por pasta: <m>/{include/oly/<m>, src, tests, bench}
    core/         utilitários sem dependências
    ecs/          o ECS por arquétipos
    input/        ações de input (ActionMap)
    app/          a Engine: loop, sistemas, eventos, schedule, estados
    bridge/       API C oly*, OlyWorld, OlyEngine, registro do esquema
  packages/       TS, um pacote npm por pasta
    core/         @oly/core: a engine no lado TS, sem DOM
    client/       @oly/client: camada do navegador
  tools/codegen/  @oly/codegen: esquema TS -> header C++
  testing/        @oly/testing (privado): jogo fixture, testes WASM, benchmarks da fronteira
games/
  arena/          jogo de validação (C++ em native/, TS em src/, teste de fumaça em tests/)
tools/            scripts de medição e de lint (Python e shell)
docs/
  arquitetura/    esta documentação
  desenvolvimento/  comandos e estilo de código
  benchmarks/     resultados registrados: ecs/, bridge/, wasm/
  superpowers/    specs (fonte da verdade) e planos de implementação por fase
.claude/          skills, agentes e hook de formatação do Claude Code
```

## Forma de um módulo C++

Cada módulo C++ segue a mesma forma:

```
engine/runtime/<m>/
  CMakeLists.txt         oly_add_module(NAME <m> DEPENDS ...)
  include/oly/<m>/       headers públicos
  include/oly/<m>/detail/  tipos internos (namespace oly::<m>::detail)
  src/                   implementação (entra por glob)
  tests/                 suítes doctest e casos de morte
  bench/                 benchmarks (só no ecs)
```

O caminho de include repete o nome do módulo (`#include "oly/ecs/world.hpp"`), então o include já diz de onde vem o header.

Veja também: [modulos-e-fronteiras.md](modulos-e-fronteiras.md).
