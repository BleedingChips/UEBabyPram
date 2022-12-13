cmake_minimum_required(VERSION 3.20)

add_executable(
	UEBabyPramUnitTest
	UnitTest.cpp
)

target_compile_features(UEBabyPramUnitTest PUBLIC cxx_std_23)
target_link_libraries(UEBabyPramUnitTest PUBLIC UEBabyPram)
