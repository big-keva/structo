link_libraries(
	structo
	DeliriX
	${MoonyCode_LIB} mtc z)

add_executable(sx-merge
	merge-indices.cpp)

add_executable(sx-check
	index-check.cpp)

add_executable(sx-stats
	index-stats.cpp)

add_executable(sx-query
	index-query.cpp)
