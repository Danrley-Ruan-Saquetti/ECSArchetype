set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OLY_BUILD_TESTS "Build the tests" ON)
option(OLY_BUILD_BENCH "Build the benchmarks" OFF)
option(OLY_SANITIZE "ASan + UBSan" OFF)
option(OLY_WERROR "Warnings become errors" ON)
option(OLY_VEC_REPORT "Vectorization report in the benchmarks" OFF)
option(OLY_BUILD_GAMES "Build the games (requires make ts first)" OFF)
set(OLY_ENABLE_ASSERTS "" CACHE STRING "0/1; empty = 0 with NDEBUG, otherwise 1")

if(EMSCRIPTEN)
  # ALLOW_MEMORY_GROWTH: the benchmarks create 1M entities. EXIT_RUNTIME=1: without it ctest does not see the exit
  # code of the program running in node.
  add_link_options(-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
endif()

if(OLY_ENABLE_ASSERTS STREQUAL "")
  if(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    set(OLY_ASSERTS_ENABLED 0)
  else()
    set(OLY_ASSERTS_ENABLED 1)
  endif()
else()
  set(OLY_ASSERTS_ENABLED ${OLY_ENABLE_ASSERTS})
endif()

if(OLY_SANITIZE)
  # Global: the dependencies are instrumented too, avoiding mixed runtimes.
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()
