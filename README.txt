中国象棋 GUI（基于 SDL2 + 皮卡鱼引擎）
========================================

项目简介
--------
使用 C++17 和 SDL2 构建的中国象棋图形界面程序，支持人机对战、人人对战、
引擎自战等模式，集成皮卡鱼（Pikafish）UCI 引擎与象棋云库查询，
并内置动态规则脚本系统，可在运行时热更新对局规则。

功能特性
--------
- 完整中国象棋规则（将帅对脸、合法走法过滤、将杀/困毙检测）
- 皮卡鱼 UCI 引擎集成（自动检测同目录引擎与 NNUE 权重）
- 执红 / 执黑独立开关，引擎可同时操控双方实现自战
- 象棋云库查询（chessdb.cn）
- 完整棋谱记录、点击跳转、PGN 导入导出
- 棋盘翻转（上下）与镜像（左右）
- 动态规则脚本（xiangqi_rules.txt），运行时修改立即生效
- 窗口自由缩放，字体与按钮随窗口高度等比例调整

编译依赖
--------
- GCC / Clang（支持 C++17）
- CMake >= 3.10
- SDL2、SDL2_ttf、SDL2_gfx
- libcurl

Ubuntu / Debian 安装依赖：
  sudo apt install build-essential cmake \
      libsdl2-dev libsdl2-ttf-dev libsdl2-gfx-dev \
      libcurl4-openssl-dev \
      fonts-noto-cjk

编译步骤
--------
  git clone <仓库地址>
  cd xiangqi-gui
  mkdir build && cd build
  cmake ..
  make -j$(nproc)
  ./xiangqi

引擎配置
--------
首次运行时，程序会在 build/ 目录下生成 xiangqi.ini 配置文件。
将皮卡鱼引擎可执行文件（如 pikafish-avx2）与 pikafish.nnue
放在同一目录，程序会自动检测并加载。

也可通过界面右侧"引擎"按钮手动选择引擎路径。

规则脚本（xiangqi_rules.txt）
------------------------------
默认规则：

  rule "被将军必须应将"
  when in_check and (not has_legal_moves)
  action checkmate

  rule "60步不吃子和棋"
  when no_capture_count >= 120
  action draw

  rule "将军提示"
  when in_check and has_legal_moves
  action warn "将军! 必须应将!"

支持的条件变量：
  in_check          当前方是否被将军（bool）
  has_legal_moves   当前方是否有合法走法（bool）
  no_capture_count  连续未吃子半步数（int）
  move_count        总走法数（int）
  current_side      当前方："red" 或 "black"
  last_captured     上一步被吃棋子类型

支持的动作：checkmate / draw / lose / warn "消息"

支持的运算符：and, or, not, (), >=, <=, ==, !=, >, <

修改该文件后，下一步走棋时自动重新加载，无需重启程序。

配置文件（xiangqi.ini）
-----------------------
  engine_path       引擎可执行文件路径
  nnue_path         NNUE 权重文件路径
  search_depth      搜索深度（默认 15）
  search_time       搜索时间毫秒（默认 3000）
  threads           引擎线程数（默认 1）
  hash_size         哈希表 MB（默认 128）
  use_cloud_book    是否使用云库（0/1）
  engine_plays_red  引擎执红（0/1）
  engine_plays_black 引擎执黑（0/1）
  flip_board        翻转棋盘（0/1）
  mirror_board      镜像棋盘（0/1）

许可证
------
MIT License

作者
----
见 src/ 目录下源代码注释。
