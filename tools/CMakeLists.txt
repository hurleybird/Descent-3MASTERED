cmake_minimum_required(VERSION 3.11)

add_subdirectory(hogdir)
if(BUILD_TESTING)
	add_subdirectory(renderer-scene-tests)
endif()
