cmake_minimum_required(VERSION 3.15)
project(ECSArchetype VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(APP_NAME "ECSArchetype")

file(GLOB_RECURSE SOURCES
  CONFIGURE_DEPENDS
  src/*.cpp
)

add_executable(${APP_NAME} ${SOURCES})

target_include_directories(${APP_NAME}
  PRIVATE
  include
)