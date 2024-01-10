add_repositories("xege-repo git@gitee.com:xege/ege-xrepo.git")
add_requires("xege 20.08")
add_requires("lua")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

target ("chess")
	add_packages("xege", "lua")
	add_defines("__STDC_LIMIT_MACROS")
	set_rundir("$(projectdir)")
	set_kind ("binary")
	add_files ("main.cc")

