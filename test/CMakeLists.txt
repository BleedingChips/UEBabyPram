cmake_minimum_required(VERSION 3.25.1)

add_executable(
	LogFilterTest
	LogFilterTest.cpp
)

ENABLE_CPP_MODULE()

target_link_libraries(LogFilterTest PUBLIC UEBabyPramModule)
