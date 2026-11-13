oly_add_module(NAME schema)

add_executable(oly-schema tool/main.cpp)
target_link_libraries(oly-schema PRIVATE oly::schema)
oly_warnings(oly-schema)
set_target_properties(oly-schema PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
