cmake_minimum_required(VERSION 3.25)
project(ECSArchetype VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ECS_BUILD_TESTS "Compila os testes" ON)
option(ECS_BUILD_BENCH "Compila os benchmarks" OFF)
option(ECS_SANITIZE "ASan + UBSan" OFF)
option(ECS_WERROR "Avisos viram erros" ON)
option(ECS_VEC_REPORT "Relatório de vetorização nos benchmarks" OFF)
set(ECS_ENABLE_ASSERTS "" CACHE STRING "0/1; vazio = 0 com NDEBUG, senão 1")

if(EMSCRIPTEN)
  add_link_options(-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
endif()

if(ECS_ENABLE_ASSERTS STREQUAL "")
  if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(_ecs_asserts 0)
  else()
    set(_ecs_asserts 1)
  endif()
else()
  set(_ecs_asserts ${ECS_ENABLE_ASSERTS})
endif()

if(ECS_SANITIZE)
  # Global: as dependências também são instrumentadas, evitando mistura de runtimes.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# Avisos só nos nossos alvos (terceiros não recebem as flags).
function(ecs_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(ECS_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()

# Exceções WASM só onde o doctest as captura (CHECK_THROWS). No-op fora do Emscripten.
function(ecs_wasm_exceptions target)
  if(EMSCRIPTEN)
    target_compile_options(${target} PRIVATE -fwasm-exceptions)
    target_link_options(${target} PRIVATE -fwasm-exceptions)
  endif()
endfunction()

# Variáveis de ambiente do processo node chegam ao getenv do teste. No-op fora do Emscripten.
function(ecs_wasm_node_env target)
  if(EMSCRIPTEN)
    target_link_options(${target} PRIVATE --pre-js ${PROJECT_SOURCE_DIR}/tools/wasm/node_env.js)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/tools/wasm/node_env.js)
  endif()
endfunction()

file(GLOB ECS_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(ecs STATIC ${ECS_SOURCES})
target_include_directories(ecs PUBLIC include)
target_compile_features(ecs PUBLIC cxx_std_20)
target_compile_definitions(ecs PUBLIC ECS_ENABLE_ASSERTS=${_ecs_asserts})
ecs_warnings(ecs)

include(FetchContent)

if(ECS_BUILD_TESTS)
  enable_testing()
  set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
  set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    SYSTEM)
  FetchContent_MakeAvailable(doctest)
  add_subdirectory(tests)
endif()

if(ECS_BUILD_BENCH)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
    SYSTEM)
  FetchContent_MakeAvailable(benchmark)
  add_subdirectory(bench)
endif()
