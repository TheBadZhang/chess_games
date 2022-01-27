project ("EGE")


add_linkdirs ("$(projectdir)")
target ("main")
	set_kind ("binary")
	add_deps("lua")
	add_files ("$(projectdir)/main.cxx")
	set_linkdirs ("$(projectdir)")
	if is_arch ("x64") then
		set_links ("graphics64")
	elseif is_arch ("x86") then
		set_links ("graphics")
	end
	set_links ("uuid", "msimg32", "gdi32", "imm32", "ole32", "oleaut32", "winmm",  "gdiplus")

-- ���� sol �����е� lua
target ("lua")
    set_kind ("static")
	set_optimize("fastest")
	if is_plat("windows") then
		set_languages("c11")
	else -- ��Ӧ������ c99 �Ŷԣ����� MSVC ��֧�� c99
		set_languages("c99")
	end
	add_includedirs("./lua", { public = true })
    add_files("./lua/*.c|lua.c|luac.c|onelua.c")
