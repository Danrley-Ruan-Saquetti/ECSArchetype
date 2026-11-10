# Exceções WASM só onde o doctest as captura (CHECK_THROWS). No-op fora do Emscripten.
function(oly_wasm_exceptions target)
  if(EMSCRIPTEN)
    target_compile_options(${target} PRIVATE -fwasm-exceptions)
    target_link_options(${target} PRIVATE -fwasm-exceptions)
  endif()
endfunction()

# Variáveis de ambiente do processo node chegam ao getenv do teste. No-op fora do Emscripten.
function(oly_wasm_node_env target)
  if(EMSCRIPTEN)
    set(nodeEnvironmentScript ${PROJECT_SOURCE_DIR}/cmake/wasm/node_env.js)
    target_link_options(${target} PRIVATE --pre-js ${nodeEnvironmentScript})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${nodeEnvironmentScript})
  endif()
endfunction()
