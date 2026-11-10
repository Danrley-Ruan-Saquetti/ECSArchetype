oly_add_module(NAME core)
target_compile_definitions(oly_core PUBLIC ECS_ENABLE_ASSERTS=${OLY_ASSERTS_ENABLED})

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
