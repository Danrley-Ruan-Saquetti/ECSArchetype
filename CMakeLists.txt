cmake_minimum_required(VERSION 3.25)
project(Oly VERSION 1.0 LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(OlyOptions)
include(OlyWarnings)
include(OlyWasm)
include(Dependencies)
include(OlyModule)
include(OlyGame)

enable_testing()
add_subdirectory(engine)
if(OLY_BUILD_GAMES)
  add_subdirectory(games)
endif()

# Boundary manifest for oly.lint.boundaries: modules and games with their DEPENDS.
function(oly_manifest_entries property outputVariable)
  get_property(entries GLOBAL PROPERTY ${property})
  set(json "")
  foreach(entry IN LISTS entries)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 name)
    list(GET fields 1 directory)
    list(GET fields 2 dependencies)
    if(dependencies STREQUAL "")
      set(dependencyArray "")
    else()
      string(REPLACE "," "\", \"" dependencyArray "\"${dependencies}\"")
    endif()
    list(APPEND json "{\"name\": \"${name}\", \"directory\": \"${directory}\", \"depends\": [${dependencyArray}]}")
  endforeach()
  list(JOIN json ", " joined)
  set(${outputVariable} "${joined}" PARENT_SCOPE)
endfunction()

oly_manifest_entries(OLY_MODULES moduleEntries)
oly_manifest_entries(OLY_GAMES gameEntries)
file(WRITE ${CMAKE_BINARY_DIR}/oly_boundaries.json "{\"modules\": [${moduleEntries}], \"games\": [${gameEntries}]}\n")

if(NOT EMSCRIPTEN OR OLY_BUILD_GAMES)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_FOUND)
    add_test(NAME oly.lint.boundaries
      COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/check_boundaries.py
              --root ${PROJECT_SOURCE_DIR} --manifest ${CMAKE_BINARY_DIR}/oly_boundaries.json)
    add_test(NAME oly.lint.boundaries_selftest
      COMMAND ${Python3_EXECUTABLE} -I ${PROJECT_SOURCE_DIR}/tools/test_check_boundaries.py)
  endif()
endif()
