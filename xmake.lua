local version = "1.0.0"
local name = "VrWorkbenchSitEventFix"

set_project(name)
set_version(version)

set_arch("x64")
set_languages("c++23")
set_warnings("allextra")

add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")

add_rules("plugin.compile_commands.autoupdate")

set_config("skyrim_se", false)
set_config("skyrim_ae", false)
set_config("skyrim_vr", true)
includes("CommonLibSSE-NG")

target(name)
	set_kind("shared")
	add_rules("commonlibsse-ng.plugin", {
	    name = name,
	    author = "nadako",
	    description = "VR Workbench Sit Event Fix"
	})
	set_pcxxheader("src/pch.h")
	add_files("src/**.cpp")

	after_build(function(target)
        import("utils.archive")

        local dist_dir = path.absolute(vformat("$(builddir)/dist"))
        local dist_mod_dir = path.join(dist_dir, name)
        local dist_archive = path.join(dist_dir, name .. "-" .. target:version() .. ".zip")
        os.tryrm(dist_dir)

        local target_dirs = {
            "C:/Games/MySkyrimVR/modlist/mods/" .. name,
            "C:/Games/SkyrimVRForMod/mods/" .. name,
            dist_mod_dir,
        }
        for _, target_dir in ipairs(target_dirs) do
            local target_plugin_dir = path.join(target_dir, "SKSE", "Plugins")
            os.mkdir(target_plugin_dir)
            os.cp(target:targetfile(), target_plugin_dir)
            os.cp(target:symbolfile(), target_plugin_dir)
            print("Deployed into " .. target_dir)
        end

        archive.archive(dist_archive, "*", {curdir = dist_mod_dir})
        print("Packaged " .. dist_archive)
	end)
target_end()