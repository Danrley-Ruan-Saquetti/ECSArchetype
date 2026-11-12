# WASM exceptions only where doctest catches them (CHECK_THROWS). No-op outside Emscripten.
function(oly_wasm_exceptions target)
  if(EMSCRIPTEN)
    target_compile_options(${target} PRIVATE -fwasm-exceptions)
    target_link_options(${target} PRIVATE -fwasm-exceptions)
  endif()
endfunction()

# Environment variables of the node process reach the test's getenv. No-op outside Emscripten.
function(oly_wasm_node_env target)
  if(EMSCRIPTEN)
    set(nodeEnvironmentScript ${PROJECT_SOURCE_DIR}/cmake/wasm/node_env.js)
    target_link_options(${target} PRIVATE --pre-js ${nodeEnvironmentScript})
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${nodeEnvironmentScript})
  endif()
endfunction()
