set_project("UEBabyPram")
option("UEBabyPramUnitTest", {default = false, description = "Enable Unit Test"})

if has_config("UEBabyPramUnitTest") then
    includes("../Potato/")
end

target("UEBabyPram")
    set_kind("static")
    add_files("UEBabyPram/*.cpp")
    add_files("UEBabyPram/*.ixx")
    add_deps("Potato")
    add_rules("mode.debug", "mode.release")
    set_languages("cxxlatest")

if has_config("UEBabyPramUnitTest") then
    target("UEBabyPramLogFilterTest")
        set_kind("binary")
        add_files("Test/LogFilterTest.cpp")
        add_deps("UEBabyPram")
        add_rules("mode.debug", "mode.release")
        set_languages("cxxlatest")
end






