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
