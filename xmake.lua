-- include subprojects
includes("lib/commonlibf4")

-- set project constants
set_project("TF3DHud")
set_version("0.1.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")
add_rules("plugin.vsxmake.autoupdate")

add_defines("COMMONLIB_RUNTIMECOUNT=3")

add_requires("imgui", { configs = { dx11 = true, win32 = true } })

-- define targets
target("TF3DHud")
    add_rules("commonlibf4.plugin", {
        name = "TF3DHud",
        author = "Bingle",
        description = "First-person HUD 3D player preview",
        plugin_template = path.join(os.projectdir(), "res/commonlibf4-plugin.cpp.in"),
    })

    -- add src files
    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    add_packages("imgui")
    add_syslinks("d3d11", "dxgi")
    set_pcxxheader("src/pch.h")
