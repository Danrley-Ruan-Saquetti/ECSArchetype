#pragma once

#include <cstdint>

#include "oly/app/engine.hpp"
#include "oly/bridge/engine_api.hpp"

namespace oly::bridge {
  // Acesso à Engine por trás do handle opaco da API C, para jogos escritos em C++.
  oly::app::Engine& engineOf(OlyEngine* engine) noexcept;

  using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t systemId);

  // Troca quem executa os sistemas de script e devolve o anterior. No WASM o padrão chama o JS; no nativo, dispara assert.
  ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept;
}
