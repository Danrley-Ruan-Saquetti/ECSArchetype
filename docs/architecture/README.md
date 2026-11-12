# Arquitetura do Oly

Esta pasta descreve o Oly como ele está hoje: o que cada parte faz, como as partes se ligam e por que a estrutura é esta. Ela não conta a história do projeto. Os requisitos detalhados ficam nas specs ([Documentos de referência](#documentos-de-referência)).

## Visão geral

O Oly é uma engine de jogos 2D para o navegador, escrita em C++20 e TypeScript. É um projeto de estudo e de benchmark: cada decisão de representação de dados tem um custo medido.

A engine tem três peças:

- **Núcleo C++**: o ECS por arquétipos e toda a lógica de engine (loop de passo fixo, fases, pausa, eventos, ações de input, schedule e estados). Ele compila para x86-64 (testes nativos) e para WebAssembly (jogos).
- **Ponte**: uma API C (`oly*`) exportada pelo módulo WASM.
- **Camada TypeScript fina**: carrega o WASM, repassa ao núcleo o tempo e o input do navegador, registra sistemas TS e dá acesso sem cópia aos componentes.

Três regras guiam o desenho:

1. **A engine está completa em C++.** Um jogo só em C++ usa todos os mecanismos sem depender de nada escrito em TS. O TS tem conveniências de API, nunca comportamento de engine.
2. **A fronteira WASM↔JS é cruzada por bloco, por lote ou por sistema, nunca por entidade** nos caminhos quentes.
3. **Existe uma única fonte para o layout dos dados**: o esquema do jogo, escrito em TS. Um gerador produz o C++ a partir dele.

```
 jogo (TS e/ou C++)                          games/<jogo>/
 ───────────────────────────────────────────────────────────────────────
 pacotes TS (camada fina)          │  núcleo C++ (WASM ou nativo)
   @oly/client: rAF, visibilidade, │    app:    Engine (loop, fases, pausa,
                teclado e mouse    │            eventos, schedule, estados)
   @oly/core:   esquema, World,    │    input:  ActionMap
                Engine, contextos, │    bridge: API C oly*, chamada de volta
                views sem cópia    │    sistemas e handlers C++ do jogo
 ──────────────── API C (extern "C") + 1 função JS importada ─────────────
 ECS: component → entity → table → archetype → query / command_buffer → world
 core: assert, align, small_map, type_name
```

## Índice

| Arquivo | Conteúdo |
|---|---|
| [repository.md](repository.md) | mapa das pastas e forma de um módulo |
| [modules-and-boundaries.md](modules-and-boundaries.md) | grafo de dependências, como as fronteiras são garantidas, regras de vocabulário |
| [core.md](core.md) | utilitários base (`OLY_ASSERT` e outros) |
| [ecs.md](ecs.md) | o ECS por arquétipos: camadas, índices, custos, iteração, `CommandBuffer` |
| [input.md](input.md) | ações de input e botões físicos HID |
| [app.md](app.md) | a `Engine`: loop, sistemas, pausa, eventos, schedule, estados, recuperação |
| [bridge.md](bridge.md) | a API C e a chamada de volta para o TS |
| [schema-and-codegen.md](schema-and-codegen.md) | o esquema do jogo, o gerador e o módulo WASM de cada jogo |
| [ts-packages.md](ts-packages.md) | `@oly/core`, `@oly/client`, `@oly/codegen`, `@oly/testing` |
| [games.md](games.md) | o jogo de validação `games/arena` |
| [build-and-tests.md](build-and-tests.md) | Docker, presets, testes e qualidade de código |
| [performance.md](performance.md) | metas, situação atual e custos da fronteira |
| [decisions.md](decisions.md) | decisões arquiteturais principais, com motivo e custo aceito |
| [limits.md](limits.md) | limites conhecidos e o que fica fora do escopo |
| [glossary.md](glossary.md) | o mesmo verbo em C++, API C e TS |

Para trabalhar no código: [../development/commands.md](../development/commands.md) (build, testes, benchmarks, jogos) e [../development/code-style.md](../development/code-style.md).

## Documentos de referência

As specs são a fonte da verdade. Os planos registram, fase a fase, como cada parte foi implementada e as decisões que as specs deixaram em aberto.

| Documento | Conteúdo |
|---|---|
| `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` | ECS: requisitos `R-*`, invariantes `I-1..I-11`, metas `NF-*` |
| `docs/superpowers/specs/2026-11-06-wasm-cost-study-design.md` | estudo de custo nativo × WASM |
| `docs/superpowers/specs/2026-11-09-game-engine-design.md` | a engine: loop, eventos, input, schedule, estados, jogo de validação |
| `docs/superpowers/specs/2026-11-09-oly-modularization-design.md` | módulos, fronteiras, nomes e superfícies públicas |
| `docs/superpowers/plans/` | planos de implementação por fase |
| `CLAUDE.md` | índice para o Claude Code e regras que valem sempre |
| `docs/benchmarks/` | resultados e situação das metas |
