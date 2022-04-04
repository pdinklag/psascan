add_executable(construct_sa main.cpp utils.cpp)
target_link_libraries(construct_sa divsufsort divsufsort64)
set_target_properties(construct_sa PROPERTIES OUTPUT_NAME ${CMAKE_BINARY_DIR}/construct_sa)
