project(stemka_api)

add_compile_options(-static-libstdc++ -fvisibility=hidden -fPIC)

add_library(ru-stemka SHARED
	src/stemming-rus.cpp
	src/language-api.cpp)

add_library(ua-stemka SHARED
	src/stemming-ukr.cpp
	src/language-api.cpp)

target_link_libraries(ru-stemka stemka moonycode)
target_link_libraries(ua-stemka stemka moonycode)

add_subdirectory(contrib/stemka)
add_subdirectory(tests)
