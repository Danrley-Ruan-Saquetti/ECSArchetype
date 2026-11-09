file(GLOB ENGINE_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(engine_core OBJECT ${ENGINE_SOURCES})
target_include_directories(engine_core PUBLIC include)
target_link_libraries(engine_core PUBLIC ecs)
ecs_warnings(engine_core)

if(ECS_BUILD_TESTS)
  # engine_add_test(<nome> <fontes...>)
  function(engine_add_test name)
    add_executable(${name} ${ARGN} ${PROJECT_SOURCE_DIR}/tests/support/doctest_main.cpp)
    target_link_libraries(${name} PRIVATE engine_core doctest::doctest)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/support)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/engine/tests)
    ecs_warnings(${name})
    ecs_wasm_exceptions(${name})
    ecs_wasm_node_env(${name})
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  engine_add_test(engine_c_api tests/c_api.cpp tests/test_schema_registry.cpp)
endif()

if(ENGINE_BUILD_FIXTURE)
  find_program(ENGINE_NODE node REQUIRED)
  set(ENGINE_GENERATOR ${PROJECT_SOURCE_DIR}/build/ts/generator/main.js)
  if(NOT EXISTS ${ENGINE_GENERATOR})
    message(FATAL_ERROR "rode 'make engine-ts' antes de configurar com ENGINE_BUILD_FIXTURE=ON")
  endif()

  # engine_add_game(<alvo> SCHEMA_JS <schema.js compilado> SOURCES <cpp...>)
  # Gera os structs e o registro do esquema, compila o jogo com a engine e liga o módulo WASM (.mjs).
  function(engine_add_game target)
    cmake_parse_arguments(GAME "" "SCHEMA_JS" "SOURCES" ${ARGN})

    set(generatedDirectory ${CMAKE_CURRENT_BINARY_DIR}/${target}_generated)
    set(generatedHeader ${generatedDirectory}/schema.generated.hpp)
    set(generatedRegistry ${generatedDirectory}/schema_registry.generated.cpp)
    add_custom_command(
      OUTPUT ${generatedHeader} ${generatedRegistry}
      COMMAND ${ENGINE_NODE} ${ENGINE_GENERATOR} --schema ${GAME_SCHEMA_JS} --out ${generatedDirectory}
      DEPENDS ${GAME_SCHEMA_JS} ${ENGINE_GENERATOR}
      COMMENT "Gerando o esquema C++ de ${target}"
      VERBATIM)

    add_executable(${target} ${GAME_SOURCES} ${generatedRegistry} ${generatedHeader})
    target_include_directories(${target} PRIVATE ${generatedDirectory})
    target_link_libraries(${target} PRIVATE engine_core)
    ecs_warnings(${target})
    set_target_properties(${target} PROPERTIES SUFFIX ".mjs" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/engine)

    if(EMSCRIPTEN)
      target_link_options(${target} PRIVATE
        -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web
        -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0)
    endif()
  endfunction()

  engine_add_game(fixture_game
    SCHEMA_JS ${PROJECT_SOURCE_DIR}/build/ts/fixture/schema.js
    SOURCES fixture/systems.cpp)

  if(ECS_BUILD_TESTS)
    add_test(NAME engine_ts_unit COMMAND ${ENGINE_NODE} --test ${PROJECT_SOURCE_DIR}/build/ts/test/unit/)
  endif()
endif()
