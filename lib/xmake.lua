-- ============================================================================
--  EGE (Easy Graphics Engine) —— 由 submodule 提供源码，编译为静态库
-- ----------------------------------------------------------------------------
--  lib/xege 是 git submodule，指向上游
--      https://github.com/x-ege/xege
--  并**固定在上游 tag `v25.11`**（commit 903320e）。
--
--  为什么用 submodule 而不是把源码拷进来：
--    拷贝进来的副本无法回答"这一份对应上游哪个版本、有没有被改过"。
--    固定到 tag 之后，版本可复现，也随时能 diff 出本地有没有动过源码。
--    取代了根 xmake.lua 原先的
--        add_repositories("xege-repo git@gitee.com:xege/ege-xrepo.git")
--        add_requires("xege 20.08")
--  即：不走 gitee SSH，且用的是更新的 25.11。
--
--  首次拉取源码（克隆本工程之后必须执行一次）：
--      git submodule update --init lib/xege
--  想升级版本：进 lib/xege 切到新 tag，然后在工程根目录 git add lib/xege，
--  把新的 commit 记进父仓库（升级后务必重跑全部自检）。
--
--  ⚠️ 3rdparty/ccap 是 xege 自己的**嵌套 submodule**（CameraCapture），
--     默认 `--init` 不会把它一起拉下来（需要 --recursive），因此这里是个
--     空目录。所以本工程刻意**不定义** EGE_ENABLE_CAMERA_CAPTURE ——
--     camera_capture.cpp 里全部逻辑都在该宏内，会退化成空实现。
--     这也与上游 CMakeLists 在「编译器不支持 C++17」时的行为一致。
--     如果将来确实要用摄像头，得先 `git submodule update --init --recursive`
--     并在这里打开该宏（届时还要把 ccap 的源文件一起加进来）。
-- ============================================================================

target("xege")
    set_kind("static")
    set_languages("c++17")

    -- 让 compile_commands.json 也覆盖 EGE 源码，便于跳转/补全
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

    -- lib/xmake.lua 里的相对路径以本文件所在目录（lib/）为基准
    add_files("xege/src/*.cpp")
    add_includedirs("xege/include", {public = true})

    -- ege_head.h 自带 #ifndef 保护，这里显式声明以对齐上游 CMake 构建
    add_defines("EGE_GRAPH_LIB_BUILD=1")

    -- MinGW 不会处理 EGE 源码里的 #pragma comment(lib, ...)（那是 MSVC 专有），
    -- 这里显式补上 graphics.cpp / 头文件所声明的全部系统库。
    -- 上游包定义（xmake-repo/packages/x/xege）也是同一组思路。
    add_syslinks(
        "gdiplus",  "gdi32",    "user32",
        "ole32",    "oleaut32", "uuid",
        "imm32",    "msimg32",  "winmm",
        "shell32"
    )

    -- 复用根 xmake.lua 定义的项目级配置（工具链 / 编码 / 静态运行库）
    chess_apply_toolchain()
    chess_apply_text_charset()
    chess_apply_static_runtime()
