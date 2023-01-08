cmake_minimum_required(VERSION 3.20)

add_library(
	UEBabyPramLib
	STATIC
	UEBabyPramLogFilter.cpp
)

target_compile_features(UEBabyPramLib PUBLIC cxx_std_23)
target_link_libraries(UEBabyPramLib PUBLIC PotatoLib)
target_include_directories(UEBabyPramLib PUBLIC "../include/")
