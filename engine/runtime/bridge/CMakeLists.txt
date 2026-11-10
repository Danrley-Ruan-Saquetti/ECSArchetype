oly_add_module(NAME bridge DEPENDS app ecs core OBJECT)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
