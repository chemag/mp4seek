cmake_minimum_required(VERSION 3.10)
project(mp4seek VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add bento4 as a subdirectory (builds libap4)
add_subdirectory(lib/bento4)

# mp4seek executable
add_executable(mp4seek mp4seek.cc)
target_link_libraries(mp4seek ap4)
