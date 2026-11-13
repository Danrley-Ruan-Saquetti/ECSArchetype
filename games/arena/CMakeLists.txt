file(GLOB nativeSources CONFIGURE_DEPENDS native/*.cpp)

oly_add_game(arena_game
  SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/dist/schema.js
  SOURCES ${nativeSources}
  DEPENDS ecs app capi)

if(OLY_BUILD_TESTS)
  add_test(NAME ts.arena.smoke COMMAND ${OLY_NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/dist/tests/)
  set_tests_properties(ts.arena.smoke PROPERTIES ENVIRONMENT OLY_ARENA_MODULE=$<TARGET_FILE:arena_game>)
endif()
