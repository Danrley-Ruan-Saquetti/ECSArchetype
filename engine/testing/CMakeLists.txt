oly_add_game(fixture_game
  SCHEMA_JS ${PROJECT_SOURCE_DIR}/engine/testing/dist/fixture/schema.js
  SOURCES fixture/systems.cpp
  DEPENDS ecs app capi)

if(OLY_BUILD_TESTS)
  add_test(NAME ts.testing.wasm COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/testing/dist/tests/wasm/)
  set_tests_properties(ts.testing.wasm PROPERTIES ENVIRONMENT OLY_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>)
endif()
