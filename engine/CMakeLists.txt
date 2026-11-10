add_subdirectory(runtime)

if(OLY_BUILD_GAMES)
  oly_add_game(fixture_game
    SCHEMA_JS ${PROJECT_SOURCE_DIR}/build/ts/fixture/schema.js
    SOURCES fixture/systems.cpp
    DEPENDS ecs bridge)

  if(OLY_BUILD_TESTS)
    add_test(NAME ts.core.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/build/ts/test/unit/)
    add_test(NAME ts.testing.wasm COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/build/ts/test/wasm/)
    set_tests_properties(ts.testing.wasm PROPERTIES ENVIRONMENT ENGINE_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>)
  endif()
endif()
