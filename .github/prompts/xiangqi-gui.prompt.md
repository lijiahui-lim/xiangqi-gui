---
description: "使用SDL2创建中国象棋GUI程序，集成皮卡鱼引擎和云库"
agent: "agent"
tools: [search, terminal, file]
---

# 中国象棋 SDL2 GUI 开发提示词

## 项目概述

完整的中国象棋图形界面程序，使用 C++17 和 SDL2 构建，支持人机/人人/引擎自战模式，集成皮卡鱼引擎与云库查询，含动态规则脚本系统。

## 技术栈

- **语言**: C++17，CMake 构建系统
- **GUI**: SDL2 + SDL2_ttf（字体渲染）+ SDL2_gfx（图形基元）
- **网络**: libcurl（云库 HTTP 查询，`std::thread` 异步）
- **引擎**: 皮卡鱼（Pikafish）UCI 协议象棋引擎
- **系统**: Linux，使用 fork/pipe 进行引擎进程通信

## 核心功能需求

### 1. 棋盘与棋子渲染
- 10×9 标准中国象棋棋盘，含河界、九宫格斜线
- 使用系统中文字体（如 Noto Sans CJK）渲染棋子文字
- 红方在下、黑方在上，支持翻转棋盘
- 棋子选中高亮和合法走法提示

### 2. 完整象棋规则
- 所有七种棋子的走法生成：将/帅、士、象/相、马、车、炮、兵/卒
- 将帅对面（照面）检测
- 合法走法过滤（走后不被将军）
- FEN 字符串解析与生成

### 3. UCI 引擎集成
- 通过 fork + pipe 启动引擎子进程
- 实现完整 UCI 协议通信（uci/isready/position/go/stop/quit）
- 解析引擎输出：info 行（depth/score/pv）、bestmove
- 自动检测同目录下的 NNUE 权重文件
- 支持引擎选项配置（线程数、Hash 大小等）

### 4. 人机/引擎对战模式
- **执红/执黑** 各自独立开关，按钮显示 ★（引擎执该方）/ ☆（人类执该方）
- 两个开关均开启时，引擎自动连续自战（auto-play 链式调用 `onEngineBestMove` → `tryStartEngineMove()`）
- 两个开关均关闭时，退化为纯人人对战模式
- 切换执棋方时**不开新局**，立即在当前局面触发引擎思考
- 核心辅助函数：`isEngineTurn()` 判断当前轮到引擎、`tryStartEngineMove()` 条件启动

### 5. 象棋云库
- 查询 chessdb.cn API（`http://www.chessdb.cn/chessdb.php?action=queryall&board=FEN`）
- 解析返回的走法和评分
- 评分转为语义描述：绝杀/大优/优势/略优/均势/略劣/劣势/大劣/必败

### 6. 棋谱功能
- 记录完整对局走法历史，每步包含中文记谱和 UCI 格式
- 侧边面板显示棋谱列表，支持滚动浏览
- **点击棋谱中任意一步可跳转到该局面**（含历史浏览时），hit-test 须在 `isViewingHistory()` 守卫之前处理
- 支持导出棋谱为 PGN 格式（带中文注释）
- 支持导入 PGN 棋谱文件并自动回放
- 棋谱显示格式：回合数 + 红方走法 + 黑方走法（如 "1. 炮二平五 马8进7"）

### 7. UCI 走法转中文记谱
- 将 UCI 格式（如 "h2e2"）转换为中文记谱（如 "炮二平五"）
- 支持完整 PV 行转换（通过临时棋盘模拟每步走法）
- 数字使用中文：一二三四五六七八九
- 红方用"进/退/平"，黑方用"进/退/平"

### 9. 窗口自适应与字体缩放
- 窗口可自由拉伸，棋盘和面板自动缩放，**字体和按钮不设上限硬帽**
- 棋子字体随格子大小缩放；面板字体：`medSz = windowH/50+4`，`smallSz = windowH/50+1`，`panelSz = windowH/50`
- 侧边面板行高 `lineH = windowH/32`（避免行间距过小导致文字叠加）
- 按钮高度 `max(24, windowH/23)`，无上限

### 10. 内置文件浏览器
- 不依赖系统对话框（zenity 等可能有兼容性问题）
- 使用 SDL2 自绘文件浏览器，支持目录导航
- 过滤显示可执行文件和包含 "pikafish" 的文件
- 用于选择和加载引擎

### 11. 引擎配置文件
- 支持 INI 格式配置文件（`xiangqi.ini`），保存在可执行文件同目录
- 启动时自动加载配置，退出/加载引擎时自动保存
- 配置项：`engine_path`、`nnue_path`、`search_depth`、`search_time`、`threads`、`hash_size`、`use_cloud_book`、**`engine_plays_red`**、**`engine_plays_black`**、`flip_board`、`mirror_board`
- 已废弃 `human_side`（旧格式向后兼容读取，不再写入）
- 简单 key=value 格式，支持 UTF-8 路径，`#` 开头为注释行

### 12. 棋盘翻转与镜像
- **翻转（flip）**：棋盘上下翻转，红方显示在上方
- **镜像（mirror）**：棋盘左右镜像，列坐标 `col → 8 - col`
- 所有坐标转换函数（`screenToBoard`、`boardToScreen`、`drawAllPieces` 等）均接受 `bool mirrored` 参数
- 镜像在翻转之前应用：先镜像列，再按 flip 方向决定行排列

### 13. 动态规则脚本系统
- 规则文件：`xiangqi_rules.txt`（与可执行文件同目录）
- 每次 `checkGameRules()` 时调用 `reloadFromFile()` 实现**热重载**（运行时修改文件立即生效）
- 文件不存在或格式不符时，自动写入默认规则

#### 脚本语法
```
rule "规则名称"
when <条件表达式>
action <动作>
```

**条件变量**：
| 变量 | 类型 | 含义 |
|------|------|------|
| `in_check` | bool | 当前方是否被将军 |
| `has_legal_moves` | bool | 当前方是否有合法走法 |
| `no_capture_count` | int | 连续未吃子半步数 |
| `move_count` | int | 总走法数 |
| `current_side` | string | `"red"` 或 `"black"` |
| `last_captured` | string | 上一步被吃棋子类型（见 board.h PieceType 名） |

**逻辑运算符**：`and`、`or`、`not`、`()`

**比较运算符**：`>=`、`<=`、`==`、`!=`、`>`、`<`

**动作**：
- `checkmate` — 将杀，当前方输
- `draw` — 判和
- `lose` — 当前方认负
- `warn "消息"` — 显示提示文字（不结束对局）

#### 默认规则示例
```
rule "被将军必须应将"
when in_check and (not has_legal_moves)
action checkmate

rule "60步不吃子和棋"
when no_capture_count >= 120
action draw

rule "将军提示"
when in_check and has_legal_moves
action warn "将军! 必须应将!"
```

### 14. 按钮布局（侧边面板，6 行）
| 行 | 按钮 |
|----|------|
| 0 | 新局 / 悔棋 / 停止 |
| 1 | 翻转 / ★☆执红 / ★☆执黑 |（`engine_plays_red/black`）
| 2 | 镜像 / — / — |
| 3 | 引擎 / 云库 / — |
| 4 | 导出PGN / 导入PGN / — |
| 5 | 引擎选项 / — / — |

> 按钮索引：0=新局,1=悔棋,2=停止,3=翻转,4=★执红,5=★执黑,6=镜像,…

## 项目结构

```
xiangqi-gui/
├── CMakeLists.txt
└── src/
    ├── main.cpp          # 入口
    ├── board.h/cpp       # 棋盘模型、规则、FEN
    ├── engine.h/cpp      # UCI 引擎通信
    ├── cloudbook.h/cpp   # 云库查询
    ├── renderer.h/cpp    # SDL2 渲染、字体、文件浏览器；坐标函数含 flip/mirror 参数
    ├── gui.h/cpp         # 游戏逻辑、事件处理；GameSettings 含 enginePlaysRed/Black/flipBoard/mirrorBoard
    └── rules.h/cpp       # 动态规则脚本引擎（tokenizer + 递归下降解析器）
```

## 关键实现细节

- 使用 `pkg_check_modules` 查找 SDL2/TTF/GFX/CURL
- 引擎通信使用非阻塞读取（`fcntl + O_NONBLOCK`）
- 云库查询异步执行，使用 `std::thread`
- 棋盘坐标系：`(col, row)`，col 0-8 从左到右，row 0-9 从上到下
- 界面布局：左侧棋盘区 + 右侧信息面板（按钮、引擎信息、云库走法、棋谱列表）
- `m_noCaptureCount`：每步走棋时更新（吃子归零，否则 +1）；悔棋时重新从走法历史计算
- 规则引擎：`RuleEngine`（`rules.h`），`GameState` 传入局面快照，`evaluate()` 返回 `RuleResult`
- `checkGameRules()` 每次调用 `reloadFromFile()` 支持热更新；文件缺失时自动写入默认规则
