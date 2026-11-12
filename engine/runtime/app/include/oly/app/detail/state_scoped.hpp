#pragma once

namespace oly::app::detail {
  // Tag de escopo: toda entidade criada com um estado ativo a recebe e a troca de estado destrói as que ainda a têm.
  struct StateScoped {};
}
