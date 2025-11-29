add_compile_definitions(
	STEMKA_SO_PATH=${stemka_api_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}ru-stemka${CMAKE_SHARED_LIBRARY_SUFFIX})

add_executable(test-structo-stemka-api
	test-lang-stemka.cpp
	../../../tests/test-main.cpp)

add_dependencies(test-structo-stemka-api ru-stemka)

target_link_libraries(test-structo-stemka-api structo moonycode mtc)
