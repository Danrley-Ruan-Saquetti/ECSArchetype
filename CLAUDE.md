# CLAUDE.md

Oly: engine de jogos 2D em C++20 e TypeScript, sobre um ECS por arquétipos single-thread (projeto de estudo e benchmark). Núcleo C++ compilado para WASM, API C `oly*`, camada TS fina.

## Documentação

Comece por `docs/architecture/README.md`. Ele tem a visão geral e o índice de toda a documentação: arquitetura, glossário, comandos, specs, planos e benchmarks.

## Regras que valem sempre

- Toolchain só no Docker: `docker compose run --rm dev ...` (C++ nativo) e `docker compose run --rm wasm ...` (WASM e TS). Não há compilador C++ no host.
- Commits, comentários e mensagens de assert em pt-BR.
- Não formate `.cpp`/`.hpp` à mão: o hook `.claude/hooks/clang-format.sh` formata após cada edição (falha se o Docker estiver parado).
- Use os nomes do glossário. Sem aliases ou `using` de compatibilidade com nomes antigos. Conceitos de jogo nunca em `engine/`; nomes de jogo só em `engine/testing/fixture/` e `games/`.
- Ao mudar `table`, `archetype`, `query*`, `command_buffer` ou `world`: mantenha `checkInvariants` cobrindo a mudança e rode os agentes `spec-conformance-reviewer` e `perf-reviewer`.
- Teste novo: use a skill `add-test-case`.

## Estilo de código

@docs/development/code-style.md
