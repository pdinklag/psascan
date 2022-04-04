cmake_minimum_required(VERSION 3.4.1 FATAL_ERROR)
project(psascan)

# set C++ build flags
set(CXX_STANDARD c++11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fopenmp ${GCC_WARNINGS}")

# options
option(USE_LIBSAIS "If set, libsais will be used rather than divsufsort" OFF)
if(USE_LIBSAIS)
    add_definitions(-DUSE_LIBSAIS)
endif()

# extlib
add_subdirectory(extlib)

# include
include_directories(${CMAKE_BINARY_DIR}/extlib/libdivsufsort/include)
include_directories(${CMAKE_SOURCE_DIR}/extlib/libsais/src)

# subdirectories
add_subdirectory(src)
add_subdirectory(tools)
