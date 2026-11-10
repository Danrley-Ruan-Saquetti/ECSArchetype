oly_add_module(NAME ecs DEPENDS core)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(OLY_BUILD_BENCH)
  add_subdirectory(bench)
endif()
