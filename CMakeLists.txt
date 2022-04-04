cmake_minimum_required(VERSION 3.4.1 FATAL_ERROR)
project(psascan)

# set C++ build flags
set(CXX_STANDARD c++11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fopenmp ${GCC_WARNINGS}")

# extlib
add_subdirectory(extlib)

# include
include_directories(${CMAKE_BINARY_DIR}/extlib/libdivsufsort/include)

# subdirectories
add_subdirectory(src)
add_subdirectory(tools)
