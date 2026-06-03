add_rules("mode.debug", "mode.release", "mode.releasedbg", "mode.profile")
set_languages("cxxlatest")

add_requires("ctre")

if os.scriptdir() == os.projectdir() then
    includes("../Potato/")
end

target("UEBabyPram")
    set_kind("static")
    add_files("UEBabyPram/*.cpp")
    add_files("UEBabyPram/*.ixx", {public=true})
    add_deps("Potato")
    add_packages("ctre")
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

if os.scriptdir() == os.projectdir() then
    
    add_requires("re2")
    
    target("UEBabyPramLogFilter")
        set_kind("binary")
        add_files("UEBabyPramLogFilter/*.cpp")
        add_files("UEBabyPramLogFilter/*.ixx")
        add_deps("Potato")
        add_deps("UEBabyPram")
        add_packages("re2")
    target_end()

    task("build-logfilter-skill")
        set_menu {
            usage       = "xmake build-logfilter-skill [options]",
            description = "Build UEBabyPramLogFilter in release mode and package as a skill directory.",
            options     = {
                {nil, "output", "kv", nil, "Output directory for the skill (default: projectdir/Skills)"}
            }
        }
        on_run(function ()
            import("core.base.option")

            local outdir   = option.get("output") or path.join(os.projectdir(), "Skills")
            local skilldir = path.join(outdir, "uebabypram-log-filter")
            local scriptsdir = path.join(skilldir, "scripts")
            local template = path.join(os.projectdir(), "UEBabyPramLogFilter", "skill-template")

            print("[1/3] Copying skill-template to %s", skilldir)
            os.tryrm(skilldir)
            os.cp(path.join(template, "*"), skilldir)

            print("[2/3] Configuring and building UEBabyPramLogFilter (release)...")
            os.exec("xmake f -m release -c")
            os.exec("xmake build UEBabyPramLogFilter")

            print("[3/3] Copying executable to %s", scriptsdir)
            os.mkdir(scriptsdir)
            local exe = path.join(os.projectdir(), "build", "windows", "x64", "release", "UEBabyPramLogFilter.exe")
            os.cp(exe, scriptsdir)

            cprint("${bright green}Skill built successfully at %s", skilldir)
        end)

end






