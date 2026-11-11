oly_add_module(NAME app DEPENDS input ecs core)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
