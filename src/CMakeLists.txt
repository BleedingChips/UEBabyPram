cmake_minimum_required(VERSION 3.20)

add_library(
	UEBabyPramLib
	STATIC
	UEBabyPramLogFilter.cpp
)

if(NOT DEFINED Potato_FOUND)
	message(FATAL_ERROR "UEBabyPram Require Potato")
endif()

target_compile_features(UEBabyPramLib PUBLIC cxx_std_23)
target_include_directories(UEBabyPramLib PUBLIC ${Potato_INCLUDE_DIRS})
target_include_directories(UEBabyPramLib PUBLIC "../include/")
target_link_libraries(UEBabyPramLib PUBLIC PotatoLib)