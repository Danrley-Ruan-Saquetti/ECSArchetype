---
name: add-test-case
description: Adiciona testes ao ECS do jeito que o build e o contrato esperam, seja uma suíte doctest nova em engine/runtime/<módulo>/tests/, um TEST_CASE numa suíte existente ou um caso de morte em contract_death (violação de contrato que deve disparar OLY_ASSERT). Use sempre que for escrever, criar ou registrar um teste neste repositório, cobrir um requisito R-* ou uma invariante I-* com teste, testar uma violação de contrato ou um assert novo, ou quando um arquivo em tests/ de um módulo parecer não estar rodando, mesmo que o usuário não diga "suíte" nem "contract_death".
argument-hint: "[suite <nome> | caso <nome> | morte <nome>]"
---

# add-test-case

Um teste novo neste repositório tem passos que não aparecem no código que ele testa. Uma suíte que não foi registrada no CMake do módulo compila zero vezes e passa sempre. Um caso de morte que ficou fora da lista do `foreach` nunca roda. Já uma substring frouxa demais faz o caso passar por causa de outro assert. Nenhum desses erros quebra o build. Esta skill existe para que o teste novo rode de verdade e falhe pelo motivo certo.

## Fronteira

**Território**: escolhe onde o teste mora (suíte nova, `TEST_CASE` numa suíte existente ou caso de morte), define a fiação que cada forma exige para ser compilada e executada, e estabelece as verificações que todo teste carrega (invariantes após cada operação, contagem de vida para componentes não triviais, substrings que identificam o assert certo).

**Fora da fronteira**: qual comportamento merece teste e qual é o resultado esperado (vem da spec e do pedido do usuário); corrigir a implementação quando o teste falha; a mensagem do `OLY_ASSERT` em si, que é escrita junto com o código que ela protege; benchmarks; criar o commit, que é ato do usuário.

**O que não preciso saber**: como `World`, `Table` e `CommandBuffer` funcionam por dentro. O teste exercita a API pública e confere o resultado com `checkInvariants(world)` (função livre de `diagnostics.hpp`), que já conhece as invariantes. Se um teste precisar ler estrutura interna para decidir se passou, provavelmente falta uma invariante, e isso é outra conversa.

**Contrato de borda**: recebo o comportamento a testar (ou a violação de contrato a provocar) e, opcionalmente, a forma; entrego o arquivo de teste alterado ou criado, a fiação no `tests/CMakeLists.txt` do módulo e a saída da execução que mostra o teste novo rodando e passando. Não faço commit.

## 1. Escolher a forma

| O que se quer provar | Forma |
|---|---|
| Comportamento correto de um módulo que já tem suíte | `TEST_CASE` na suíte existente do módulo |
| Módulo ou recurso novo, sem suíte que o represente | Suíte nova (arquivo novo em `tests/`) |
| Uso proibido pelo contrato aborta com `OLY_ASSERT` | Caso de morte em `contract_death.cpp` do módulo (`ecs` ou `bridge`) |
| Exceção do código do usuário atravessa o ECS sem corromper estado | `CHECK_THROWS` numa suíte doctest |

Por que a violação de contrato não pode ser um `TEST_CASE`: o handler padrão de `OLY_ASSERT` chama `std::abort()`, o que derruba o executável inteiro da suíte. Além disso, `OLY_ASSERT` some com `OLY_ENABLE_ASSERTS=0`, e as suítes doctest também rodam no preset `release`. Por isso um `TEST_CASE` nunca pode depender de um assert disparar. Casos de morte só existem com asserts ligados (`OLY_ENABLE_ASSERTS`). `contract_death` só é compilado quando os asserts estão ligados, e por isso esses casos moram lá.

Na dúvida entre suíte nova e existente, prefira a existente: cada suíte é um executável a mais para compilar e linkar.

## 2a. Suíte nova

Dois passos, e o segundo é o que se esquece:

1. Crie `engine/runtime/<módulo>/tests/<nome>.cpp`.
2. Registre em `engine/runtime/<módulo>/tests/CMakeLists.txt`, junto dos outros `oly_add_test`:

   ```cmake
   oly_add_test(MODULE ecs NAME <nome> SOURCES <nome>.cpp)
   ```

   O teste aparece no ctest como `<módulo>.<nome>` e o executável é `build/<preset>/tests/<módulo>_<nome>`. Fontes de apoio entram em `SOURCES` (ex.: `support/alloc_counter.cpp`, se a suíte usar `alloc_counter.hpp`). Os testes **não** entram por glob: um `.cpp` em `tests/` sem essa linha simplesmente não existe para o build. Já as fontes de `src/` entram por glob, o que costuma confundir.

O que `oly_add_test` já faz e você não deve repetir: liga `doctest_main.cpp` (de `engine/runtime/core/tests/support/`) (então o arquivo **não** define `main` nem `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`), põe o `tests/support` do módulo no include path (por isso é `"components.hpp"`, sem caminho), aplica os avisos com `-Werror` e registra o executável no CTest com o mesmo nome.

Modelo:

```cpp
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "components.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("remove de componente preserva os valores das outras entidades") {
  oly::ecs::World world;
  std::vector<oly::ecs::Entity> entities;
  for (uint64_t i = 0; i < 16; ++i) {
    entities.push_back(world.spawn(A{i}, B{i, 2 * i}));
    CHECK_INVARIANTS(world);
  }

  world.remove<B>(entities[3]);
  CHECK_INVARIANTS(world);

  for (uint64_t i = 0; i < 16; ++i) {
    CHECK(world.get<A>(entities[i]).value == i);
  }
}

TEST_CASE("I-2: remove de LifeCounter destrói exatamente um valor") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    auto entity = world.spawn(A{1}, LifeCounter{7});
    CHECK(LifeCounter::alive == 1);

    world.remove<LifeCounter>(entity);
    CHECK_INVARIANTS(world);
    CHECK(LifeCounter::alive == 0);
  }
  CHECK(LifeCounter::alive == 0);
}
```

## 2b. Caso numa suíte existente

Só o `TEST_CASE`; a fiação já existe. Confira que o arquivo aparece num `oly_add_test` antes de confiar que ele roda: um arquivo órfão em `tests/` é exatamente o defeito que esta skill previne.

## 2c. Caso de morte

`<módulo>_contract_death` (`ecs_contract_death`, `bridge_contract_death`) é um executável sem doctest. Cada caso é um argumento de linha de comando. O handler instalado confere se a mensagem do assert contém todas as substrings exigidas: sai com 0 se contém e com 2 se falta alguma. Se o caso terminar sem assert, `main` devolve 1. Um caso novo mexe em **três** lugares, e os três precisam usar o mesmo nome:

1. **A função**, dentro do `namespace` anônimo de `engine/runtime/<módulo>/tests/contract_death.cpp`, em `camelCase`, sem retorno e sem parâmetros:

   ```cpp
   void removeDuringEach() {
     oly::ecs::World world;
     auto entity = world.spawn(A{1}, B{2, 3});
     world.query<A>().each([&](A&) { world.remove<B>(entity); });
   }
   ```

2. **A entrada na tabela `cases`** de `main`, com o nome em `snake_case` e as substrings exigidas:

   ```cpp
   {"remove_during_each", removeDuringEach, {"remove", "e="}},
   ```

3. **O nome em `CASES`** na chamada `oly_add_contract_death(MODULE <módulo> SOURCE contract_death.cpp CASES ...)` de `tests/CMakeLists.txt`. Esta lista é o que cria o teste CTest `<módulo>.contract_death.<nome>`. Sem ela o caso compila, existe na tabela e **nunca roda**, e nada avisa.

Se o nome estiver na lista mas faltar na tabela, o executável sai com 64 ("caso desconhecido") e o teste falha. Esse erro aparece sozinho; o contrário, não.

### Escolher as substrings

O handler recebe a mensagem completa, montada por `assertFail` como:

```
OLY_ASSERT failed: <expressão> | <mensagem formatada> (<arquivo>:<linha>)
```

A busca é por `strstr` na linha inteira, então uma substring pode casar com a **expressão** ou com o **caminho do arquivo**, e não só com a mensagem. Escolha substrings que só a mensagem do assert pretendido teria:

- **A operação** (`"add"`, `"flush"`, `"remove"`), que é o que separa este assert dos vizinhos.
- **`"e="`** quando o assert é sobre uma entidade: confirma que a mensagem identifica a entidade, como o CLAUDE.md exige.
- **O nome do tipo**, quando o assert o imprime. Use um tipo local com nome próprio (`struct Position { float x; };` dentro da função, como em `getDeadEntity`), não `A` ou `B`: uma letra solta casa com quase qualquer mensagem e não prova nada.

Antes de confiar no caso, leia o `OLY_ASSERT` alvo em `src/` e copie as substrings do formato dele.

### Uma violação por vez

Vários asserts juntam duas condições numa só, como `isAlive(entity) && iterating_ == 0` em `destroy`, `add` e `remove`, e imprimem a mesma mensagem para qualquer uma das duas. Um caso "durante iteração" que usa uma entidade já destruída passa, mas pelo motivo errado. Monte o cenário de modo que só a violação pretendida esteja presente: entidade viva quando o caso é sobre iteração, sem iteração quando o caso é sobre entidade morta.

O handler sai no primeiro assert. Nada depois da violação roda, então não ponha verificações depois dela.

### Assert novo junto com o caso

Se o caso cobre um `OLY_ASSERT` que ainda vai ser escrito, a mensagem dele segue a regra do CLAUDE.md (operação, `Entity` como `e=%u:%u`, nome do tipo). As substrings do caso cobram exatamente essa regra, e é por isso que valem a pena.

## 3. As verificações que todo teste carrega

**`CHECK_INVARIANTS(world)` depois de cada operação estrutural** (spawn, destroy, add, remove, flush). Não basta uma vez no fim: uma operação que corrompe o estado e outra que o "conserta" por acaso passam numa checagem final. A macro está em `components.hpp`, chama a função livre `oly::ecs::checkInvariants` e mostra o motivo da falha. Ela não existe em `contract_death`, que não tem doctest e onde o processo morre antes de qualquer checagem.

**Valores derivados da entidade ou do índice** (`A{i}`, `B{i, 2 * i}`), não constantes. Quando todas as linhas valem `A{1}`, uma troca de linhas mal feita num swap-remove continua dando o valor certo. Com valores distintos, ela aparece como valor errado.

**`LifeCounter` sempre que o teste envolve um componente não trivial** (move, remove, destroy, flush com componente que tem destrutor). Ele conta construções menos destruições e pega moved-from não destruído e double destroy (I-2). Três cuidados:

- `LifeCounter::alive` é `static inline`, compartilhado por todos os `TEST_CASE` do executável. Zere no início do caso.
- Ponha o `World` num bloco `{ }` e confira `alive == 0` **depois** do bloco: é isso que prova que o destrutor do `World` destruiu tudo o que sobrou.
- Os tipos triviais (`A`, `B`, `Wide`) vão pelo caminho `memcpy` e não exercitam move/destroy. Se o código testado tem os dois caminhos, cubra os dois.

**Aleatoriedade**: se o teste sortear operações, use a semente e o número de operações de `engine/runtime/ecs/tests/support/model.hpp` (`fuzzSeed()`, `fuzzOperationCount()`, variáveis `OLY_FUZZ_SEED` e `OLY_FUZZ_OPS`) e imprima a semente no stderr, como as suítes de fuzz já fazem. Um teste aleatório sem semente reproduzível falha uma vez e nunca mais.

## 4. Provar que roda

Os comandos de build e teste, e como filtrar um teste pelo nome, estão na seção "Build e testes" do CLAUDE.md. Tudo roda no container, no preset padrão (ASan + UBSan + asserts).

- **Suíte ou caso doctest:** rode só a suíte afetada (`make test T='^ecs\.<nome>'`) e confirme na saída que o número de casos subiu. "0 test cases" ou um filtro sem nenhum teste encontrado significa que a fiação falhou, não que passou.
- **Caso de morte:** rode o executável direto, `build/debug-asan/tests/<módulo>_contract_death <nome>`, e leia a mensagem impressa no stderr. Ela precisa ser a do assert pretendido, não só "algum assert". Depois rode o filtro `<módulo>.contract_death.<nome>` pelo CTest para confirmar que o `foreach` criou o teste.
- **Prova de que o teste detecta o defeito**, quando for barato: troque uma substring por uma que não existe (o caso deve sair com 2) ou comente a linha que viola o contrato (deve sair com 1), rode e desfaça. Um teste que nunca foi visto falhando não foi visto testando.

Se um teste existente quebrar, pare e relate: decidir se o defeito está no teste ou no código está fora desta skill.

## Relatório final

Liste os arquivos tocados e, para cada teste novo, uma linha: nome, forma (suíte nova, caso em suíte existente, caso de morte), o que ele prova e a evidência de que rodou (contagem de casos na saída do doctest, ou a mensagem do assert impressa pelo `contract_death`). Proponha a mensagem de commit (`test: ...`, em pt-BR) sem commitar.
