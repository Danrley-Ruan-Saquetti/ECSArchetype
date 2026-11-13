add_subdirectory(runtime)

if(EMSCRIPTEN)
  add_subdirectory(environments/web/host/native)
else()
  # oly-schema runs at build time, so it is built for the machine; WASM builds use the schema-tool preset's.
  add_subdirectory(schema/codegen)
  add_subdirectory(environments/web/clients/typescript/codegen)
endif()

if(OLY_BUILD_GAMES)
  if(OLY_BUILD_TESTS)
    add_test(NAME ts.core.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/environments/web/clients/typescript/core/dist-tests/unit/)
    add_test(NAME ts.web_host.unit COMMAND ${OLY_NODE} --test ${PROJECT_SOURCE_DIR}/engine/environments/web/host/js/dist-tests/)
  endif()

  add_subdirectory(testing)
endif()
