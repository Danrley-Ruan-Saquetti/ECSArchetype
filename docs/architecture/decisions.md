# Decisões arquiteturais principais

| Decisão | Motivo | Custo aceito |
|---|---|---|
| ECS por arquétipos com tabelas SoA | iteração próxima de um SoA escrito à mão | transições movem a entidade entre tabelas |
| Arestas `±ComponentId` memoizadas com mapeamento de colunas | transição sem hash nem alocação | memória por arquétipo |
| Arquétipos nunca removidos | ponteiros estáveis, queries sem invalidação | arquétipos vazios ficam |
| `detail/` + lint em vez de pimpl | templates quentes fazem inline | fronteira garantida pelo lint, não pelo compilador |
| API template e API `ById` separadas | o caminho quente não paga indireção | duas implementações a manter |
| `CommandBuffer` com uma transição por entidade | flush barato em lote | complexidade de coalescing (coberta por fuzz) |
| Loop e toda regra de engine em C++ | um jogo só em C++ não fica com engine incompleta | o TS precisa da chamada de volta |
| Uma única função JS importada, com `kind` | sem tabela de funções que cresce; várias engines por módulo | despacho por `kind` no TS |
| Esquema TS como fonte única + codegen + hash | layout nunca diverge entre C++ e TS | WASM compilado por jogo |
| Coluna de structs, view com passo | não muda a `Table` nem a API de queries | acessor com passo no TS |
| `Engine` não é dona do `World` | o ECS fica abaixo e independente da engine | duas `Engine`s no mesmo `World` dividem a tag de escopo |
| Escopo de estado por tag interna | limpeza por query, `persist` barato | arquétipos gêmeos com e sem a tag |
| Schedule como min-heap no `app`, não como entidades | tarefas não aparecem nas queries do jogo | escopo das tarefas implementado à parte |
| Input em módulo próprio, sem interface de plataforma | regras de borda testáveis sem `Engine`; o host só empurra eventos | — |
| `@oly/core` sem DOM, `@oly/client` com DOM | o núcleo TS roda em node, navegador e worker | dois pacotes |

Os detalhes de cada decisão estão nos arquivos dos módulos ([README.md](README.md#índice)) e nas specs.
