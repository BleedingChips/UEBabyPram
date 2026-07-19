add_requires("zlib")
add_requires("tbb")
add_requires("blake3")
add_requires("xxhash")


target("UEBabyPramMiniUESource")
    set_kind("static")
    set_languages("cxxlatest")

    add_packages("zlib")
    add_packages("tbb")
    add_packages("blake3")
    add_packages("xxhash")

    -- Define Begin
    add_defines("WITH_EDITOR=0", {public=true})
    add_defines("WITH_ENGINE=0", {public=true})
    add_defines("WITH_UNREAL_DEVELOPER_TOOLS=0", {public=true})
    add_defines("WITH_PLUGIN_SUPPORT=0", {public=true})
    add_defines("IS_MONOLITHIC=0", {public=true})
    add_defines("IS_PROGRAM=1", {public=true})
    add_defines("WITH_SERVER_CODE=0", {public=true})
    add_defines("_UNICODE=1", {public=true})
    add_defines("UNICODE=1", {public=true})
    add_defines("USE_MALLOC_BINNED3=0", {public=true})
    add_defines("USE_MALLOC_BINNED2=1", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION=0", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION_PG=0", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION_PG_PROFILING=0", {public=true})
    add_defines("PLATFORM_COMPILER_OPTIMIZATION_LTCG=0", {public=true})
    add_defines("USE_STATS_WITHOUT_ENGINE=0", {public=true})
    add_defines("COMPATIBLE_CHANGELIST=11", {public=true})
    add_defines("STRSAFE_LIB=1", {public=true})
    add_defines("ENABLE_NAN_DIAGNOSTIC=0", {public=true})
    add_defines("UE_AUTORTFM_ENABLED=0", {public=true})
    add_defines("UE_EXTERNAL_PROFILING_ENABLED=0", {public=true})
    add_defines("__UNREAL__=1", {public = true})
    add_defines("ENGINE_IS_LICENSEE_VERSION=0", {public = true})
    add_defines("ENGINE_VERSION_MAJOR=5", {public = true})
    add_defines("ENGINE_VERSION_MINOR=8", {public = true})
    add_defines("ENGINE_VERSION_HOTFIX=4", {public = true})
    add_defines("CURRENT_CHANGELIST=110", {public = true})
    add_defines("BRANCH_NAME=\"release\"", {public = true})
    add_defines("BUILD_VERSION=\"1\"", {public = true})
    add_defines("ENGINE_IS_PROMOTED_BUILD=1", {public = true})
    add_defines("UE_WITH_DEBUG_INFO=0", {public = true})
    add_defines("BUILD_SOURCE_URL=\"abc\"", {public = true})
    add_defines("BUILD_USER=\"abc\"", {public = true})
    add_defines("BUILD_USERDOMAINNAME=\"abc\"", {public = true})
    add_defines("BUILD_MACHINENAME=\"abc\"", {public = true})
    add_defines("UE_PERSISTENT_ALLOCATOR_RESERVE_SIZE=2147483648ULL", {public = true})
    add_defines("UE_VFS_PATHS=\"Z:/\"", {public = true})
    add_defines("UE_MODULE_NAME=\"Core\"", {public=true})
    add_defines("NO_LOGGING=1", {public=true})
    --add_defines("ENGINE_VERSION_STRING=\"Core\"", {public=true})
    add_defines("UBT_MODULE_MANIFEST=\"UnrealEditor.modules\"", {public = true})
    -- Define End

    add_defines("UBT_MODULE_MANIFEST_DEBUGGAME=\"UnrealEditor-Win64-DebugGame.modules\"", {public=true})
    if is_mode("debug") then
        add_defines("UE_BUILD_DEBUG=1", {public=true})
    else
        add_defines("UE_BUILD_DEVELOPMENT=1", {public=true})
    end

    if is_plat("windows") then
        add_defines("PLATFORM_WINDOWS=1", {public=true})
        add_defines("UBT_COMPILED_PLATFORM=Windows", {public=true})
        local v = winos.version()
        local winver = string.format("0x%02X%02X", v:get("major"), v:get("minor"))
        add_defines("WINVER=" .. winver, {public=true})
        add_links("Gdi32.lib")
        add_links("Winmm.lib")
        add_links("shell32.lib")
        add_links("iphlpapi.lib")
        add_links("Setupapi.lib")
        add_links("Netapi32.lib")
        add_links("Synchronization.lib")
    end
    
    add_includedirs("./")

    -- Core
    add_files("./Runtime/Core/**.cpp")
    add_headerfiles("./Runtime/Core/**.h")
    add_includedirs("./Runtime/Core/Private/")
    add_includedirs("./Runtime/Core/Public/", {public = true})
    add_includedirs("./Runtime/Core/Internal/")
    add_defines("CORE_API=", {public = true})
    set_pcxxheader("./Runtime/Core/Public/CoreSharedPCH.h")
    -- Core End

    -- AtomicQueue Begin
    add_includedirs("./Runtime/AutoRTFM/Public/", {public = true})
    -- AtomicQueue End

    -- Oodle Begin
    add_includedirs("./Runtime/OodleDataCompression/Sdks/2.9.14/include/")
    if is_plat("windows") then
        add_linkdirs("./Runtime/OodleDataCompression/Sdks/2.9.14/lib/Win64/")
        if is_mode("debug") then
            add_links("oo2core_win64_debug.lib")
        else
            add_links("oo2core_win64.lib")
        end
    end
    -- Oodle End

    -- BuildSetting Begin
    add_files("./Runtime/BuildSettings/**.cpp")
    add_headerfiles("./Runtime/BuildSettings/**.h")
    add_includedirs("./Runtime/BuildSettings/Private/")
    add_includedirs("./Runtime/BuildSettings/Public/", {public = true})
    add_defines("BUILDSETTINGS_API=", {public = true})
    -- BuildSetting End
    
    -- AutoRTFM Begin
    add_files("./RunTime/AutoRTFM/**.cpp")
    add_headerfiles("./RunTime/AutoRTFM/**.h")
    add_includedirs("./RunTime/AutoRTFM/Private/")
    add_includedirs("./RunTime/AutoRTFM/Public/", {public = true})
    add_defines("AUTORTFM_API=", {public = true})
    -- AutoRTFM End


    -- TraceLog Begin
    add_files("./Runtime/TraceLog/**.cpp")
    add_headerfiles("./Runtime/TraceLog/**.h")
    add_includedirs("./Runtime/TraceLog/Private/")
    add_includedirs("./Runtime/TraceLog/Public/", {public = true})
    add_defines("TRACELOG_API=", {public = true})
    -- TraceLog End


    -- TraceAnalyze Begin
    add_files("./Developer/TraceAnalysis/**.cpp")
    add_headerfiles("./Developer/TraceAnalysis/**.h")
    add_includedirs("./Developer/TraceAnalysis/Private/", {public = true})
    add_includedirs("./Developer/TraceAnalysis/Public/", {public = true})
    add_defines("TRACEANALYSIS_API=", {public = true})
    -- TraceAnalyze End

    -- TargetPlatform Begin
    add_includedirs("./Developer/TargetPlatform/Public/")
    add_defines("TARGETPLATFORM_API=", {public = true})
    -- TargetPlatform End

    --TraceService Begin
    add_files("./Developer/TraceServices/**.cpp")
    add_headerfiles("./Developer/TraceServices/**.h")
    add_includedirs("./Developer/TraceServices/Private/", {public = true})
    add_includedirs("./Developer/TraceServices/Public/", {public = true})
    add_defines("TRACESERVICES_API=", {public = true})
    --TraceService End

    --Cbor Begin
    add_files("./Runtime/Cbor/**.cpp")
    add_headerfiles("./Runtime/Cbor/**.h")
    add_includedirs("./Runtime/Cbor/Private/")
    add_includedirs("./Runtime/Cbor/Public/", {public = true})
    add_defines("CBOR_API=", {public = true})
    --Cbor End

    --SymsLib Begin
    add_files("./Runtime/SymsLib/syms/symslib.c")
    add_headerfiles("./Runtime/SymsLib/**.h")
    add_includedirs("./Runtime/SymsLib/syms/", {public = true})
    add_includedirs("./Runtime/SymsLib/", {public = true})
    add_defines("SYMS_API=", {public = true})
    --SymsLib End

    --Launch Begin
    add_headerfiles("./Runtime/Launch/**.h")
    add_includedirs("./Runtime/Launch/", {public = true})
    add_defines("SYMS_API=", {public = true})
    --Launch End
    
    add_includedirs("./Developer/DerivedDataCache/Public/")
    add_includedirs("./Developer/DesktopPlatform/Public/")
    add_includedirs("./RunTime/ImageCore/Public/")
    add_includedirs("./ThirdParty/AtomicQueue/")
    add_defines("DESKTOPPLATFORM_API=", {public = true})
    add_defines("IMAGECORE_API=", {public = true})
    
target_end()