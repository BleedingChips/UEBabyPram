cmake_minimum_required(VERSION 3.25.1)

ENABLE_CPP_MODULE()

add_library(
	UEBabyPramModule
	STATIC
	"UEBabyPramLogFilter.cpp"
)

target_sources(
	UEBabyPramModule PUBLIC
	FILE_SET cxx_modules TYPE CXX_MODULES
	FILES
		"UEBabyPramLogFilter.ixx"
)

target_link_libraries(UEBabyPramModule PUBLIC PotatoModule)
