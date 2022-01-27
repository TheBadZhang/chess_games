project ("EGE")

include ("./lib/xege/xmake.lua")
include ("./lib/sol/xmake.lua")

target ("chess")
	set_kind ("binary")
	add_deps("lua", "xege")
	add_files ("main.cpp")

