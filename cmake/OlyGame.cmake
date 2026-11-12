find_program(OLY_NODE node)
set(OLY_GENERATOR ${PROJECT_SOURCE_DIR}/engine/tools/codegen/dist/main.js)

# oly_add_game(<target> SCHEMA_JS <compiled schema.js> SOURCES <cpp...> DEPENDS <m...>)
# Generates the schema structs and registration, compiles the game with the modules and links the WASM module (.mjs).
function(oly_add_game target)
  cmake_parse_arguments(GAME "" "SCHEMA_JS" "SOURCES;DEPENDS" ${ARGN})

  if(NOT OLY_NODE)
    message(FATAL_ERROR "node not found: it runs the schema generator")
  endif()
  if(NOT EXISTS ${OLY_GENERATOR})
    message(FATAL_ERROR "run 'make ts' before configuring with OLY_BUILD_GAMES=ON")
  endif()

  set(generatedDirectory ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated)
  set(generatedHeader ${generatedDirectory}/schema.generated.hpp)
  set(generatedRegistry ${generatedDirectory}/schema_registry.generated.cpp)
  add_custom_command(
    OUTPUT ${generatedHeader} ${generatedRegistry}
    COMMAND ${OLY_NODE} ${OLY_GENERATOR} --schema ${GAME_SCHEMA_JS} --out ${generatedDirectory}
    DEPENDS ${GAME_SCHEMA_JS} ${OLY_GENERATOR}
    COMMENT "Generating the C++ schema for ${target}"
    VERBATIM)

  add_executable(${target} ${GAME_SOURCES} ${generatedRegistry} ${generatedHeader})
  target_include_directories(${target} PRIVATE ${generatedDirectory})
  foreach(dependency IN LISTS GAME_DEPENDS)
    target_link_libraries(${target} PRIVATE oly::${dependency})
  endforeach()
  oly_warnings(${target})
  set_target_properties(${target} PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/games)

  if(EMSCRIPTEN)
    # EXIT_RUNTIME=0 overrides the global =1: TS calls the module after it loads, so the runtime must not exit.
    target_link_options(${target} PRIVATE
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web
      -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0)
  endif()

  oly_register_target(OLY_GAMES ${target} ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DEPENDS})
endfunction()
