set_project("UEBabyPram")
add_rules("mode.debug", "mode.release")
set_languages("cxxlatest")

includes("../Potato/")

target("UEBabyPram")
    set_kind("static")
    add_files("UEBabyPram/*.cpp")
    add_files("UEBabyPram/*.ixx")
    add_deps("Potato")

target("UEBabyPramLogFilterTest")
    set_kind("binary")
    add_files("Test/LogFilterTest.cpp")
    add_deps("UEBabyPram")


