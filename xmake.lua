option("UEBabyPramMainProject", {default = false, description = "Enable Unit Test"})

add_rules("mode.debug", "mode.release")
set_languages("cxxlatest")

if has_config("UEBabyPramMainProject") then
    set_project("UEBabyPram")
    includes("../Potato/")
end

target("UEBabyPram")
    set_kind("static")
    add_files("UEBabyPram/*.cpp")
    add_files("UEBabyPram/*.ixx")
    add_deps("Potato")

if has_config("UEBabyPramMainProject") then
    target("UEBabyPramLogFilterTest")
        set_kind("binary")
        add_files("Test/LogFilterTest.cpp")
        add_deps("UEBabyPram")
        add_rules("mode.debug", "mode.release")
        set_languages("cxxlatest")
end






