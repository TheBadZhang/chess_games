project ("chesses")

add_repositories("xege-repo git@gitee.com:xege/ege-xrepo.git")
add_requires("xege 20.08")
add_requires("lua")


target ("chess")
	add_packages("xege", "lua")
	set_kind ("binary")
	add_files ("main.cc")

