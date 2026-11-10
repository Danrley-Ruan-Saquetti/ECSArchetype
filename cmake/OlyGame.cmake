find_program(OLY_NODE node)
set(OLY_GENERATOR ${PROJECT_SOURCE_DIR}/engine/tools/codegen/dist/main.js)

# oly_add_game(<alvo> SCHEMA_JS <schema.js compilado> SOURCES <cpp...> DEPENDS <m...>)
# Gera os structs e o registro do esquema, compila o jogo com os módulos e liga o módulo WASM (.mjs).
function(oly_add_game target)
  cmake_parse_arguments(GAME "" "SCHEMA_JS" "SOURCES;DEPENDS" ${ARGN})

  if(NOT OLY_NODE)
    message(FATAL_ERROR "node não encontrado: ele roda o gerador do esquema")
  endif()
  if(NOT EXISTS ${OLY_GENERATOR})
    message(FATAL_ERROR "rode 'make ts' antes de configurar com OLY_BUILD_GAMES=ON")
  endif()

  set(generatedDirectory ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated)
  set(generatedHeader ${generatedDirectory}/schema.generated.hpp)
  set(generatedRegistry ${generatedDirectory}/schema_registry.generated.cpp)
  add_custom_command(
    OUTPUT ${generatedHeader} ${generatedRegistry}
    COMMAND ${OLY_NODE} ${OLY_GENERATOR} --schema ${GAME_SCHEMA_JS} --out ${generatedDirectory}
    DEPENDS ${GAME_SCHEMA_JS} ${OLY_GENERATOR}
    COMMENT "Gerando o esquema C++ de ${target}"
    VERBATIM)

  add_executable(${target} ${GAME_SOURCES} ${generatedRegistry} ${generatedHeader})
  target_include_directories(${target} PRIVATE ${generatedDirectory})
  foreach(dependency IN LISTS GAME_DEPENDS)
    target_link_libraries(${target} PRIVATE oly::${dependency})
  endforeach()
  oly_warnings(${target})
  set_target_properties(${target} PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/games)

  if(EMSCRIPTEN)
    # EXIT_RUNTIME=0 anula o =1 global: o TS chama o módulo depois de carregado, então o runtime não pode encerrar.
    target_link_options(${target} PRIVATE
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web
      -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0)
  endif()

  oly_register_target(OLY_GAMES ${target} ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DEPENDS})
endfunction()
