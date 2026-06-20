

add_requires("lz4")

target("UEBabyPramMiniUESource")
    set_kind("static")
    set_languages("cxxlatest")
    if is_mode("debug") then
        add_defines("UE_BUILD_DEBUG=1", {public=true})
    else
        add_defines("UE_BUILD_DEVELOP=1", {public=true})
    end
    if is_plat("windows") then
        add_defines("PLATFORM_WINDOWS=1", {public=true})
        add_defines("UBT_COMPILED_PLATFORM=Windows", {public=true})
        local v = winos.version()
        local winver = string.format("0x%02X%02X", v:get("major"), v:get("minor"))
        add_defines("WINVER=" .. winver, {public=true})
    end
    add_defines("WITH_EDITOR=0", {public=true})
    add_defines("WITH_ENGINE=1", {public=true})
    add_defines("WITH_UNREAL_DEVELOPER_TOOLS=0", {public=true})
    add_defines("WITH_PLUGIN_SUPPORT=0", {public=true})
    add_defines("IS_MONOLITHIC=1", {public=true})
    add_defines("IS_PROGRAM=1", {public=true})
    add_defines("WITH_SERVER_CODE=0", {public=true})
    add_defines("_UNICODE=1", {public=true})
    add_defines("UNICODE=1", {public=true})
    add_defines("USE_MALLOC_BINNED3=0", {public=true})
    add_defines("USE_MALLOC_BINNED2=1", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION=0", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION_LTCG=0", {public=true})
    add_defines("USE_STATS_WITHOUT_ENGINE=0", {public=true})
    add_defines("STRSAFE_LIB=1", {public=true})
    add_defines("ENABLE_NAN_DIAGNOSTIC=0", {public=true})

    add_files("Developer/**.cpp")
    add_files("Runtime/**.cpp")
    add_files("*.cpp")
    add_files("*.ixx", {public=true})

    add_headerfiles("./**.h")

    add_includedirs("./")

    add_includedirs("./Developer/TraceAnalysis/Public/", {public = true})
    add_includedirs("./Developer/TraceAnalysis/Private/", {public = true})
    add_includedirs("./Runtime/Core/Public/", {public = true})
    add_includedirs("./Runtime/Core/Private/", {public = true})
    add_includedirs("./Runtime/TraceLog/Public/", {public = true})

    add_defines("TRACEANALYSIS_API=", {public = true})
    add_defines("CORE_API=", {public = true})
    add_defines("TRACELOG_API=", {public = true})
    add_defines("AUTORTFM_INFER=", {public = true})
    add_defines("UE_AUTORTFM_OPEN=", {public=true})
    add_defines("UE_AUTORTFM=0", {public=true})
target_end()