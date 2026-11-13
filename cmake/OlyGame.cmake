find_program(OLY_NODE node)
set(OLY_SCHEMA_TOOL ${PROJECT_SOURCE_DIR}/build/schema-tool/bin/oly-schema CACHE FILEPATH "oly-schema executable that oly_add_game runs")

# oly_add_game(<target> SCHEMA <file.oly> SOURCES <cpp...> DEPENDS <m...>)
# Generates the schema structs and registration with oly-schema, compiles the game with the modules and links the WASM module (.mjs) with the web host's script invoker.
function(oly_add_game target)
  cmake_parse_arguments(GAME "" "SCHEMA" "SOURCES;DEPENDS" ${ARGN})

  if(NOT EXISTS ${OLY_SCHEMA_TOOL})
    message(FATAL_ERROR "oly-schema not found at ${OLY_SCHEMA_TOOL}: run 'make schema-tool' (or 'make ts') before configuring with OLY_BUILD_GAMES=ON")
  endif()

  set(generatedDirectory ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated)
  set(generatedHeader ${generatedDirectory}/schema.generated.hpp)
  set(generatedRegistry ${generatedDirectory}/schema_registry.generated.cpp)
  add_custom_command(
    OUTPUT ${generatedHeader} ${generatedRegistry}
    COMMAND ${OLY_SCHEMA_TOOL} ${GAME_SCHEMA} --emit cpp=${generatedDirectory}
    DEPENDS ${GAME_SCHEMA} ${OLY_SCHEMA_TOOL}
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
    target_link_libraries(${target} PRIVATE oly::web_host)
    # EXIT_RUNTIME=0 overrides the global =1: TS calls the module after it loads, so the runtime must not exit.
    target_link_options(${target} PRIVATE
      -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web
      -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0)
  endif()

  oly_register_target(OLY_GAMES ${target} ${CMAKE_CURRENT_SOURCE_DIR} ${GAME_DEPENDS})
endfunction()
