cmake_minimum_required(VERSION 3.20)

project(
	UEBabyPramUnitTest
	DESCRIPTION "UnitTest For UEBabyPram"
	VERSION 1.0
	LANGUAGES C CXX
)

set(Potato_DIR "../Potato/cmake")

find_package(Potato CONFIG)

add_subdirectory(${Potato_SUBDIRECTORY} "Potato")

add_subdirectory("src")
add_subdirectory("test")