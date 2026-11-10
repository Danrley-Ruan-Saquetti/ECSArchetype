#pragma once

#include <cstdint>

#include "oly/app/engine.hpp"
#include "oly/bridge/engine_api.hpp"

namespace oly::bridge {
  // Acesso à Engine por trás do handle opaco da API C, para jogos escritos em C++.
  oly::app::Engine& engineOf(OlyEngine* engine) noexcept;

  // `kind` é OLY_SCRIPT_SYSTEM (`id` é o sistema; `events` nulo e `eventCount` 0) ou OLY_SCRIPT_HANDLER (`id` é o
// handler; `events` é o lote, com o layout do struct do evento).
  using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);

  // Troca quem executa os sistemas de script e devolve o anterior. No WASM o padrão chama o JS; no nativo, dispara assert.
  ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept;
}
