

includes("../MiniUESource/")


target("UEBabyPramInsightParser")
    set_kind("static")
    set_languages("cxxlatest")
    add_files("./**.cpp")
    add_files("./**.ixx", {public=true})
    add_deps("Potato")
    add_deps("UEBabyPramMiniUESourceTraceAnalysis")
target_end()





