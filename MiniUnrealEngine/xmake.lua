target("UE_CORE")
    set_kind("static")
    add_files("Core/Private/Containers/**.cpp")
    add_headerfiles("Core/Public/Containers/**.h")
    add_includedirs("Core/Public/", {public = true})
    add_defines("WITH_EDITOR=0", {public = true})
    add_defines("WITH_ENGINE=1", {public = true})
    add_defines("WITH_UNREAL_DEVELOPER_TOOLS=0", {public = true})
    add_defines("WITH_PLUGIN_SUPPORT=0", {public = true})
    add_defines("IS_PROGRAM=1", {public = true})
    add_defines("IS_MONOLITHIC=1", {public = true})
    if is_mode("debug") then
        add_defines("UE_BUILD_DEBUG=1", {public = true})
    elseif is_mode("release") then
        add_defines("UE_BUILD_DEVELOP=1", {public = true})
    end
    if is_plat("windows") then
        add_defines("PLATFORM_WINDOWS=1", {public = true})
        add_defines("UBT_COMPILED_PLATFORM=Windows", {public = true})
    end
target_end()


target("UE_TraceAnalysis")
    set_kind("static")
    add_files("TraceAnalysis/Private/Analysis/**.cpp")
    add_includedirs("TraceAnalysis/Private/")
    add_deps("UE_CORE")
target_end()






