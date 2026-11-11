oly_add_module(NAME input DEPENDS core)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
