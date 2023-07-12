cmake_minimum_required(VERSION 3.25.1)

project(
	UEBabyPram
	DESCRIPTION "Tool For UE Develop"
	VERSION 1.0
	LANGUAGES C CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	add_subdirectory("../Potato" "Potato")
endif()

ENABLE_CMAKE_CPP_MODULE()
ENABLE_CPP_MODULE()

add_library(
	UEBabyPramModule
	STATIC
	"UEBabyPram/UEBabyPramLogFilter.cpp"
)

target_sources(
	UEBabyPramModule PUBLIC
	FILE_SET cxx_modules TYPE CXX_MODULES
	FILES
		"UEBabyPram/UEBabyPramLogFilter.ixx"
)

target_link_libraries(UEBabyPramModule PUBLIC PotatoModule)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	
	add_executable(
		LogFilterTest
		"Test/LogFilterTest.cpp"
	)

	target_link_libraries(LogFilterTest PUBLIC UEBabyPramModule)

endif()


