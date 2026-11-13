oly_add_module(NAME capi DEPENDS app input ecs core OBJECT)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
