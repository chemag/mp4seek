cmake_minimum_required(VERSION 3.10)
project(mp4seek VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add bento4 as a subdirectory (builds libap4)
add_subdirectory(lib/bento4)

# mp4seek library
add_library(mp4seek_lib STATIC src/mp4seek.cc src/mp4extract.cc)
set_target_properties(mp4seek_lib PROPERTIES OUTPUT_NAME mp4seek)
target_include_directories(mp4seek_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mp4seek_lib ap4)

# mp4seek executable
add_executable(mp4seek tools/mp4seek_main.cc)
target_link_libraries(mp4seek mp4seek_lib)
