add_subdirectory(runtime)

if(OLY_BUILD_GAMES)
  if(OLY_BUILD_TESTS)
    add_test(NAME ts.core.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/environments/web/clients/typescript/core/dist-tests/unit/)
    add_test(NAME ts.codegen.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/tools/codegen/dist-tests/)
    add_test(NAME ts.client.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/packages/client/dist-tests/)
  endif()

  add_subdirectory(testing)
endif()
