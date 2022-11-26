cmake_minimum_required(VERSION 3.20)

project(
	UEBabyPramUnitTest
	DESCRIPTION "UnitTest For UEBabyPram"
	VERSION 1.0
	LANGUAGES C CXX
)

add_subdirectory("src")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	add_subdirectory("../Potato" "Potato")
	add_subdirectory("test")
endif()


