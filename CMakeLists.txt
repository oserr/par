cmake_minimum_required(VERSION 3.23)
project(par CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

if (NOT CMAKE_BUILD_TYPE OR (CMAKE_BUILD_TYPE STREQUAL ""))
  set(CMAKE_BUILD_TYPE "Debug")
endif()


string(TOLOWER "${CMAKE_BUILD_TYPE}" build_type)
message(STATUS "Building ${CMAKE_PROJECT_NAME} in ${build_type} mode")

if (build_type STREQUAL "debug")
  add_compile_options(-Wall -Wextra -Wpedantic -Og)
else()
  add_compile_definitions(NDEBUG)
  add_compile_options(-Wall -Wextra -Wpedantic -Ofast)
endif()

include(FetchContent)

# Googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.13.0)
FetchContent_MakeAvailable(googletest)

add_library(par STATIC
  src/par.h)

set_target_properties(par PROPERTIES CXX_STANDARD 23 LINKER_LANGUAGE CXX)
target_compile_features(par PRIVATE)
target_link_libraries(par ${CMAKE_THREADS_LIBS_INIT})

# A function to create an example.
function(create_example target)
  add_executable(${target} examples/${target}.cc)
  set_target_properties(${target} PROPERTIES CXX_STANDARD 23)
  target_include_directories(${target} PUBLIC src)
  target_compile_features(${target} PRIVATE)
  target_link_libraries(${target} par)
endfunction()

create_example(hello_world)
create_example(for_each)
