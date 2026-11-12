# `core`

Utilitários sem dependências, usados por todos os módulos (`engine/runtime/core/`):

- `assert.hpp`: `OLY_ASSERT(cond, fmt, ...)`. Some com `OLY_ENABLE_ASSERTS=0`. A mensagem inclui a operação, a `Entity` e o nome do tipo. O handler é trocável, o que permite os testes de morte ([build-and-tests.md](build-and-tests.md#testes)).
- `align.hpp`: alinhamento e alocação alinhada.
- `small_map.hpp`: mapa pequeno por vetor ordenado, usado nas arestas e no índice de componentes do ECS.
- `type_name.hpp`: nome de tipo em tempo de compilação, para mensagens.
