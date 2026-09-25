-- ============================================================================
--  chessgame —— 2D 棋类游戏合集
-- ----------------------------------------------------------------------------
--  棋子图集：res/chesses.txt 定义坐标 + res/chesses100.png 提供像素
--  工具链  ：默认 MinGW/GCC，用 `xmake f --toolchain_mingw=n` 可切回 MSVC
--  中 文  ：默认以 GBK 作为执行字符集（见 chess_apply_text_charset 的说明）
-- ============================================================================

-- ---------------------------------------------------------------------------
-- 可配置项
-- ---------------------------------------------------------------------------

-- 工具链开关。默认 MinGW；想用 MSVC 时：
--     xmake f --toolchain_mingw=n -c
option("toolchain_mingw")
    set_default(true)
    set_showmenu(true)
    set_description("使用 MinGW/GCC 工具链构建（默认）。设为 n 则使用 MSVC")

-- 界面中文的编码。默认 gbk 时源码 UTF-8 -> 编译期转 GBK，
-- 与 EGE 默认的 EGE_CODEPAGE_ANSI(CP_ACP) 对齐；
-- 若某些环境不支持 -fexec-charset=GBK，改成 utf8 即可（代码无需修改）：
--     xmake f --text_charset=utf8 -c
option("text_charset")
    set_default("gbk")
    set_values("gbk", "utf8")
    set_showmenu(true)
    set_description("界面中文编码：gbk（默认，配合 EGE 默认 codepage=936）或 utf8")

-- ---------------------------------------------------------------------------
-- 项目级公共配置助手
--   必须在 includes("lib") 之前定义：lib/xmake.lua 会调用它们。
--   在 target() 作用域内调用即可作用于当前 target。
-- ---------------------------------------------------------------------------

-- 选择工具链。
function chess_apply_toolchain()
    if has_config("toolchain_mingw") then
        set_toolchains("mingw")
    end
end

-- 中文文案的编码。
--
-- 为什么可以安全地用窄字符 + GBK：
--   EGE 的文本渲染（lib/xege/src/font.cpp）在窄字符重载里做的是
--       MultiByteToWideChar(getcodepage(), ...) -> DrawTextW / TextOutW
--   而 _graph_setting::codepage（src/ege_head.h）没有显式初值，属于全局零初始化，
--   即 EGE_CODEPAGE_ANSI(0) == CP_ACP，在中文 Windows 上就是 GBK(936)。
--   所以只要让窄字符串字面量也是 GBK 字节，两端就对得上。
--   代码里仍会在启动时显式调用 setcodepage(...)，不依赖这个隐含默认值。
--
--   已核实本机 MinGW（MinGW-Builds 16.1.0）自带 libiconv，
--   故 -fexec-charset=GBK 可用。-finput-charset 显式声明为 UTF-8，
--   避免依赖 GCC 的默认假设（lib/xege 的 70 个源文件已逐一验证为合法 UTF-8）。
--
-- ⚠️ 千万别用 {tools = {"gcc", "clang"}} 这类过滤器来区分编译器。
--    xmake 的 tools 过滤器匹配的是**工具名**，而 MinGW 工具链注册的工具名是
--        cc / cxx / cpp / as / ld / sh / ar / strip / ranlib / objcopy / mrc / dlltool
--    （见 toolchains/mingw/xmake.lua），**没有 "gcc" 这个名字**。
--    于是这些 flag 会被**静默丢弃** —— 编译照常成功、没有任何警告，
--    但源码里的中文保持 UTF-8 字节，被 EGE 当成 GBK 解释 -> **界面中文全部乱码**。
--    曾因此踩过一次，排查方式是直接看 .vscode/compile_commands.json 里的真实命令行。
--
--    这里改为按本项目自己的工具链开关分支（该开关与 chess_apply_toolchain()
--    一一对应，覆盖 mingw / msvc 两种受支持的情况）。
function chess_apply_text_charset()
    local utf8 = (get_config("text_charset") == "utf8")

    -- 代码里用它来做运行时 setcodepage(...)，必须与执行字符集一致
    if utf8 then
        add_defines("CHESS_TEXT_CODEPAGE=EGE_CODEPAGE_UTF8")
    else
        add_defines("CHESS_TEXT_CODEPAGE=EGE_CODEPAGE_ANSI")
    end

    local flags
    if has_config("toolchain_mingw") then
        -- GNU 系（MinGW / GCC / clang）
        if utf8 then
            flags = {"-finput-charset=UTF-8", "-fexec-charset=UTF-8"}
        else
            flags = {"-finput-charset=UTF-8", "-fexec-charset=GBK"}
        end
    else
        -- MSVC。注意 /execution-charset 支持 ".十进制代码页" 形式
        if utf8 then
            flags = {"/source-charset:utf-8", "/execution-charset:utf-8"}
        else
            flags = {"/source-charset:utf-8", "/execution-charset:.936"}
        end
    end

    add_cxflags(flags[1], flags[2])

    -- 🔑 把 flag 本身也算进一个宏里。
    --
    -- 为什么需要这样做（踩过一个大坑）：
    --   改 add_cxflags 的写法、但没改任何 *define* 时，xmake **不会**让已有目标
    --   文件失效。于是旧的 .obj 会被继续链接进程序 —— 那些编译单元里仍是旧编码
    --   的字符串字面量。症状是**界面里只有一部分中文乱码**（来自没重建的那几个
    --   .cc），而不乱的那部分恰好是"刚好被改过、因此被重建"的文件。
    --   这个现象极难定位：编译零警告、编译命令也完全正确。
    --
    --   把 flag 派生出一个数值签名编进宏之后，flag 一变宏就变，所有目标文件都会
    --   失效并自动重建，不需要人工记得执行 xmake -r。
    --
    -- 另外：改 --text_charset 选项时 CHESS_TEXT_CODEPAGE 本身就会变，也会触发重建；
    -- 这个签名专门覆盖"选项值没变、只改了 flag 生成逻辑"的情形。
    local sig = 7
    for _, f in ipairs(flags) do
        for i = 1, #f do
            sig = (sig * 31 + f:byte(i)) % 1000000007
        end
    end
    add_defines("CHESS_CHARSET_SIG=" .. sig)
end

-- MinGW 下静态链接运行库，免去随程序分发
-- libstdc++-6.dll / libgcc_s_seh-1.dll / libwinpthread-1.dll。
function chess_apply_static_runtime()
    if has_config("toolchain_mingw") then
        add_ldflags("-static", {force = true})
    end
end

-- ---------------------------------------------------------------------------
-- 工程
-- ---------------------------------------------------------------------------

-- 注意（xmake 3.0 的一个尖锐边界，踩过一次）：
--   只要本文件里出现 option() 块，xmake 为了收集 option 会再以「受限的 root
--   作用域」加载一次工程文件；在那一趟加载中，add_rules / add_files 这类
--   **target 作用域**的 API 全部是 nil，直接报
--       attempt to call a nil value (global 'add_rules')
--   而 target() 块内部的 API 在那一趟里是正常的，所以：
--       * add_rules 之类的 target API 一律写进 target() 内部；
--       * 只有 option() 与本文件定义的助手函数留在最外层。
--
-- 引入 lib/xmake.lua（其中定义 target("xege")），必须在消费它的 target 之前
includes("lib")

target("chess")
    set_kind("binary")
    set_languages("c++17")

    -- 生成 .vscode/compile_commands.json（顺带让 clangd / IntelliSense 拿到真实命令行）
    add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"})

    -- 运行期工作目录固定为项目根，这样代码里用 "res/chesses100.png"
    -- 这类相对路径即可，无需处理 exe 所在目录
    set_rundir("$(projectdir)")

    add_files("src/**.cc")
    add_includedirs("src")

    -- 供 MSVC 老版本 CRT 的 stdint.h 使用
    add_defines("__STDC_LIMIT_MACROS")

    -- 用本地 xege 静态库，取代原来的 add_packages("xege")
    add_deps("xege")

    chess_apply_toolchain()
    chess_apply_text_charset()
    chess_apply_static_runtime()

