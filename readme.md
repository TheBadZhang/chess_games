# chessgame

2D 棋类游戏合集，基于 [EGE (Easy Graphics Engine)](https://xege.org)，用 C++17 + xmake 构建。

内置四个游戏，每个都有 5 级 AI 与**分级提示**（等级越高，给出的分析信息越具体而不是越吵）。

---

## 快速开始

先拉取 EGE 源码（**必须做一次**，否则编译找不到 `lib/xege/include`）：

```sh
git submodule update --init lib/xege
```

然后构建：

```sh
xmake f -c -p windows -a x64 -m release
xmake
xmake r
```

默认使用 **MinGW/GCC**。想切回 MSVC：

```sh
xmake f --toolchain_mingw=n -c
```

> `lib/xege` 是指向 [x-ege/xege](https://github.com/x-ege/xege) 的 git submodule，
> 并**固定在上游 tag `v25.11`**（commit `903320e`）。以本地静态库方式编译，
> 不走 gitee SSH 包仓库。
>
> 固定 tag 而不是跟随分支，是为了让构建可复现、也随时能 `git diff` 出本地有没有改过 EGE 源码。
> 升级方式：进 `lib/xege` 切到新 tag，回工程根目录 `git add lib/xege` 把新 commit 记进父仓库，
> **然后重跑下面全部自检**。
>
> ⚠️ EGE 的 `3rdparty/ccap`（摄像头采集）是它自己的嵌套 submodule，默认不会被一起拉下来，
> 因此 `lib/xmake.lua` 刻意不定义 `EGE_ENABLE_CAMERA_CAPTURE`
> （`camera_capture.cpp` 的全部逻辑都在该宏内，会退化成空实现）。
> 要用摄像头需 `git submodule update --init --recursive` 并同步打开该宏、把 ccap 源码加进构建。

### 命令行工具

```sh
xmake r chess --shot menu           # 离屏渲染一帧到 build/shot_menu.png
xmake r chess --shot game:go        # 渲染某个游戏界面（不弹窗，便于核对布局）
xmake r chess --atlas-dump          # 输出棋子图集对照图 build/atlas_dump.png
xmake r chess --dump-board          # 打印象棋"棋子 -> 图集精灵"映射表
xmake r chess                       # 正常启动图形界面
```

自检命令（都返回非 0 退出码，便于接 CI）：

```sh
xmake r chess --flip-test      # 翻转棋求解器 vs 暴力枚举
xmake r chess --gomoku-test    # 五子棋规则 / 禁手 / AI 行为
xmake r chess --perft          # 象棋走子规则（perft 44 / 1920 / 79666）
xmake r chess --xq-piece       # 象棋棋子「底盘 + 字面」叠层是否都画出来
xmake r chess --go-test        # 围棋提子 / 禁自杀 / 打劫 / 数子
xmake r chess --ai-budget      # AI 计时器是否每次都重置（等级设置是否真的生效）
xmake r chess --encoding       # 中文编码链路（源码 -> 执行字符集 -> 渲染）
xmake r chess --ui-layout      # 字号调大后文字有没有溢出容器
xmake r chess --hit-test       # 绘制位置与命中测试是否一致（"点不准"类问题）
xmake r chess --gfx-selftest   # EGE 离屏绘制与 alpha 处理
xmake r chess --wininfo        # 窗口客户区 vs 画布尺寸（坐标偏差类问题）
```

### 改字号 / 换字体之前先读这段

`setfont(px, ...)` 里的 `px` 是 GDI 的 **lfHeight（字符单元高度）**，
不是字形本身的尺寸。Microsoft YaHei 的行距约占 25%，所以

```
实际汉字边长 ≈ px × 0.75        setfont(21) 画出来只有 ~16px 见方
```

`--ui-layout` 会把实测曲线打出来，想得到 N px 见方的汉字就选表里最接近 N 的 px：

```
px->glyph: 13:8 14:9 15:9 16:10 17:12 18:12 19:13 20:14 21:16 22:16
           23:17 24:18 25:19 26:19 27:20 28:21 29:21 30:22 31:24 32:25
           33:25 34:25   <- 32 以后基本不再涨
```

调完字号别忘了跑 `--ui-layout`：它用 `text::wrappedHeight()` 按真实字宽
算出换行后的高度，再和 `menu_scene.h` / `atlas_scene.h` 里的布局常量比对。
排版常量都放在头文件的 `*_layout` 命名空间里，就是为了让自检用同一份数值，
不会出现"自检抄了份过时的数字"。

---

## 游戏

| 游戏 | 规则要点 | 棋盘 |
|---|---|---|
| **翻转棋** | 点一格翻「该格 + 上下左右」共 5 格，把全盘翻成同一种颜色。单人解谜，计步数 | 4×4 ~ 8×8 |
| **五子棋** | 先连成五子者胜（长连也算）；可选**黑棋禁手**（长连 / 双四 / 双活三） | 9 / 13 / 15 / 19 |
| **象棋** | 马腿、象眼、炮隔子、兵卒过河、士将九宫、**飞将**、将军/将死/困毙、长将判负、60 回合无吃子和棋 | 9×10 固定 |
| **围棋** | 中国规则数子：提子、**禁自杀**、**简单劫**、停一手、双方停手终局；贴目按棋盘大小 | 9 / 13 / 19 |

### 操作

| 按键 | 作用 |
|---|---|
| 鼠标左键 | 落子（象棋是"点起点 → 点终点"） |
| `U` | 悔棋 |
| `R` | 重新开始 |
| `H` | 提示开 / 关 |
| `1` ~ `5` | 切换 AI 等级 |
| `空格` | 停一手（围棋） |
| `ESC` | 返回菜单 |

菜单里可配置：变体、棋盘尺寸、AI 等级、执子（先手 / 后手 / 双人对下）、规则开关、让子。

---

## 分级 AI 与提示

等级同时决定 **AI 的棋力** 与 **提示的信息量**。设计原则是"高阶给出更有用的结论"，
而不是"标记更多" —— 例如围棋 L1 只标合法点，L4/L5 才给胜率与主变。

| 等级 | 提示内容 | AI 做法 |
|---|---|---|
| L1 入门 | 只标出能走 / 不能走的点（禁手、禁着、自杀点用灰叉） | 随机 / 极浅搜索 |
| L2 初级 | 追加危险预警（会被吃、被打吃、对手成五等） | 浅层搜索 / 启发式 |
| L3 中级 | 推荐最佳着法 + 局面评估分 | 搜索 |
| L4 高级 | 给出主要变化（后续几手）与搜索深度 | 更深搜索 |
| L5 大师 | 给出**胜率**、更深搜索、杀棋 / 打劫等专项结论 | 最深 / 专项搜索 |

各游戏的引擎：

- **翻转棋** —— `GF(2) Exact`：把规则建模成 GF(2) 线性方程组精确求解，能给出
  **最少步数**、完整解法序列、解空间维数与"所有解都必须按的格子"
- **五子棋** —— `Alpha-Beta + VCF`：候选点剪枝 + 棋型打分 + 迭代加深 + 连续冲四搜索
- **象棋** —— `Alpha-Beta + TT`：子力值 + 位置表 + 置换表 + 吃子静态搜索 + 将军延伸 + killer
- **围棋** —— `MCTS + Heuristic`：L1/L2 启发式；L3~L5 蒙特卡洛树搜索
  （playout 含提子优先、避免自填眼等策略）

> 围棋 19 路在 L5 下仍算"能下但弱"，这是预期结果而不是 bug ——
> 随机模拟在 19 路上的质量天然有限。

---

## 项目结构

```
src/
  core/     types.h（Side/Coord/Move/HintData/DrawContext）、rng.h、stopwatch.h、config.h
  ui/       theme.h、text（窄字符 + GBK）、painter（面板/按钮/计量条）、
            board_view（落格与交叉点两套棋盘共用一套几何）、atlas（棋子图集）、
            gfx_util（离屏 alpha 修复）
  games/    game.h（IGame 接口）、registry（注册表）、
            flip_puzzle / gomoku(+gomoku_eval) / xiangqi / go
  ai/       ai.h（IAI 接口）、gf2（GF(2) 求解器）、
            flip_ai / gomoku_ai / xiangqi_ai / go_ai
  app/      app（场景栈 + 主循环）、scene、ai_worker（AI 工作线程）、
            menu_scene / game_scene（四个游戏共用）/ atlas_scene、selftest
lib/        xege/（EGE 25.11 源码）+ xmake.lua（本地静态库 target）
res/        chesses100.png（棋子图集，224x96，每格 32px）
chesses.txt 精灵表：名字 + (x, y, w, h)，坐标与 chesses100.png 像素 1:1 对应
```

### 加一个新游戏

1. `src/games/<game>.h/.cc`：实现 `IGame`，提供 `<game>Desc()` 与工厂
2. `src/ai/<game>_ai.h/.cc`：实现 `IAI`（若需要搜索）
3. `src/games/registry.cc` 的 `allGames()` 里加一行
4. 建议在 `src/app/selftest.cc` 加一个自检入口

---

## 一些实现上的取舍

**中文文案**用**窄字符 + 编译期 GBK**（`-fexec-charset=GBK`），与 EGE 默认的
`codepage = CP_ACP` 对齐。若某环境不支持该转码，改成 `xmake f --text_charset=utf8 -c`
即可，**代码无需修改**。

控制台输出一律 **ASCII** —— VS Code 终端是 UTF-8，直接打中文会乱码。

**AI 在独立工作线程里跑**，搜索对象是局面的深拷贝，所以 AI 思考期间仍可悔棋 /
重开 / 返回菜单，不会和主线程抢数据；结果经原子量与互斥量交回主线程应用。

**离屏绘制必须显式传目标 `PIMAGE`**。EGE 里"直接写像素缓冲"与"走 GDI"两条路径
对 alpha 的处理不同，省略目标参数会让精灵被画到窗口上而不是目标图，
表现为"棋盘格有、棋子没有"。详见 `src/ui/gfx_util.h`。

**棋子素材**：`chesses.txt` 里的字面名没有标明哪个是红哪个是黑，且缺少红方「炮」。
当前映射见 `src/games/xiangqi.cc` 的 `kSpriteMap`，用 `xmake r chess --dump-board`
可以打印出来核对；缺图时自动退化为"木盘 + 汉字"。

---

## 测试

每个游戏都有针对**规则正确性**的自动化自检（而不是靠手感）：

- 翻转棋：GF(2) 求解器与暴力枚举逐一对拍（3×3 全部 512 个局面 + 4×4 抽样），
  并验证"4×4 任意随机局面只有约 6% 可解"这一约束（所以局面必须由打乱生成）
- 五子棋：4 个方向的成五、长连、双四、双活三禁手、成五优先于禁手、
  AI 必须抓成五、必须挡冲四、AI 自对弈能终局
- 象棋：**perft 深度 1~3 = 44 / 1920 / 79666**（走子规则最客观的检验），
  外加马腿与飞将的定向用例
- 围棋：提子（含整块）、禁自杀、简单劫（立即提回非法 / 找劫材后可提）、
  双方停手终局、数子、**悔棋时整块被提的每一枚都要复原**
- `--hit-test`：棋盘绘制位置与命中测试是否严格一致（覆盖落格与交叉点两类棋盘）
