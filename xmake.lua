add_rules("mode.debug", "mode.release")
set_languages("cxxlatest")

if os.scriptdir() == os.projectdir() then
    set_project("UEBabyPram")
    includes("../Potato/")
end

target("UEBabyPram")
    set_kind("static")
    add_files("UEBabyPram/*.cpp")
    add_files("UEBabyPram/*.ixx")
    add_deps("Potato")
target_end()

if os.scriptdir() == os.projectdir() then
    set_project("UEBabyPram")

    for _, file in ipairs(os.files("Test/*.cpp")) do

        local name = path.basename(file)

        target(name)
            set_kind("binary")
            add_files(file)
            add_deps("UEBabyPram")
        target_end()

    end
end






