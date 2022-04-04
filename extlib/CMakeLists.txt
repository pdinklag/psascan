if(USE_LIBSAIS)
    # libsais
    add_library(sais STATIC ${CMAKE_CURRENT_SOURCE_DIR}/libsais/src/libsais.c)
    add_library(sais64 STATIC ${CMAKE_CURRENT_SOURCE_DIR}/libsais/src/libsais64.c)
else()
    # divsufsort
    set(BUILD_DIVSUFSORT64 ON)
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory(libdivsufsort)
endif()
