include_directories(../../)

link_libraries(structo DeliriX ${MoonyCode_LIB} mtc)

add_executable(structo-simple-index	simple-index.cpp)
add_executable(structo-quick-start	quick-start.cpp)

