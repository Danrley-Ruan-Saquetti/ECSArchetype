# Estilo de código

## Idioma

Commits, comentários e mensagens de assert são em pt-BR.

## Formatação

- `.clang-format`: base Google, indentação 2, `NamespaceIndentation: All`, sem limite de coluna.
- Um hook `PostToolUse` do Claude Code (`.claude/hooks/clang-format.sh`) roda `clang-format -i` no container `dev` após cada edição de `.cpp`/`.hpp`. Não formate à mão. Se o Docker estiver parado, o hook falha.
- `-Wall -Wextra -Wpedantic -Werror` em todos os alvos (`oly_warnings`): aviso quebra o build. TS em `strict`.

## Regras de C++

- Chaves em todo `if`/`for`/`while`/`else`, mesmo com uma linha só.
- Nomes completos, sem abreviações (`entity`, `componentId`, `sourceToDestination`, não `e`, `cid`, `srcToDst`).
- Tipos em `PascalCase`, funções e variáveis em `camelCase`, membros privados com sufixo `_`.
- Comentários só para o porquê ou para contratos não óbvios; nada que repita o código. Se um comentário explica o quê, prefira um nome ou uma função extraída.
- Linhas em branco separam ideias (blocos lógicos), não linhas soltas.
- Um bloco `namespace` por arquivo, sem reabrir o mesmo namespace mais abaixo.
- Em `class`/`struct`, os atributos (membros de dados, inclusive privados e estáticos) ficam no topo, antes de construtores e métodos. Numa `class` isso significa abrir com uma seção `private:` só de dados (e dos tipos aninhados que eles usam), depois `public:` e, por fim, `private:` com os métodos auxiliares. Mover blocos de membros não pode mudar a ordem relativa entre eles (ordem de inicialização, `-Wreorder`).
- `#include` dos headers do projeto com aspas (`"oly/ecs/..."`), depois dos headers da biblioteca padrão.
- Mensagens de `OLY_ASSERT` incluem a operação, a `Entity` e o nome do tipo.
