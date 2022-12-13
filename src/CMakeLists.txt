cmake_minimum_required(VERSION 3.20)

add_library(
	UEBabyPram
	STATIC
	UEBabyPramLogFilter.cpp
)

target_compile_features(UEBabyPram PUBLIC cxx_std_23)
target_link_libraries(UEBabyPram PUBLIC Potato)
target_include_directories(UEBabyPram PUBLIC "../include/")
