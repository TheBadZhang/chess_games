project ("chesses")

include ("./lib/sol/xmake.lua")
include ("./lib/xege/xmake.lua")

target ("chess")
	set_kind ("binary")
	add_deps("lua", "xege")
	add_files ("main.cpp")

