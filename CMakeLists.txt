cmake_minimum_required(VERSION 3.10)

# project version
project(mp4seek VERSION 0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add bento4 as a subdirectory (builds libap4)
add_subdirectory(lib/bento4)

# mp4seek library
add_library(mp4seek_lib STATIC src/mp4seek.cc src/mp4extract.cc)
set_target_properties(mp4seek_lib PROPERTIES OUTPUT_NAME mp4seek)
target_include_directories(mp4seek_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mp4seek_lib ap4)

# Generate config.h with version info
# https://cmake.org/cmake/help/latest/guide/tutorial/index.html#adding-a-version-number-and-configured-header-file
configure_file(src/config.h.in config.h)
target_include_directories(mp4seek_lib PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")

# mp4seek executable
add_executable(mp4seek tools/mp4seek_main.cc)
target_link_libraries(mp4seek mp4seek_lib)

# Tests (optional, enabled with -DBUILD_TESTING=ON)
option(BUILD_TESTING "Build tests" OFF)
if(BUILD_TESTING)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  enable_testing()

  add_executable(mp4seek_test tests/mp4seek_test.cc)
  target_link_libraries(mp4seek_test mp4seek_lib GTest::gtest_main)

  include(GoogleTest)
  gtest_discover_tests(mp4seek_test WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
