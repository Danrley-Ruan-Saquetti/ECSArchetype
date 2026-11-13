# OBJECT: the emitter registers itself from a namespace-scope initializer, which a static library would drop.
oly_add_module(NAME schema_typescript DEPENDS schema OBJECT)
target_link_libraries(oly-schema PRIVATE oly::schema_typescript)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
