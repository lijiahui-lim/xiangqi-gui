# Xiangqi GUI - Copilot Instructions

## Project Overview
- Chinese Chess (中国象棋) GUI, C++17, SDL2 + SDL2_ttf + SDL2_gfx + libcurl
- CMake build system, build directory: `build/`
- UCI engine (pikafish) via fork/pipe, cloud book API (chessdb.cn) via libcurl async
- Modes: human vs engine, human vs human, engine self-play

## Code Conventions

### Language
- Code identifiers in English
- Comments in Chinese (中文注释)
- Git commit messages in **English**

### File Size & Organization
- Every `.cpp` file must not exceed **500 lines**; split by logical function groups if exceeded
- GUI class (`gui.h`): `gui_core.cpp` (init/config/main loop/render), `gui_engine.cpp` (engine interaction/analysis), `gui_game.cpp` (game logic/moves/rules/notation), `gui_ui.cpp` (input events/keyboard), `gui_pgn.cpp` (PGN/history/edit mode/timers), `gui_filebrowser.cpp` (file browser/engine loading)
- Renderer class (`renderer.h`): `renderer_core.cpp` (init/layout/board/pieces/arrows/text), `renderer_panel.cpp` (side panel/buttons/settings/file browser/move list/clock), `renderer_edit.cpp` (edit palette)
- Rules engine (`rules.h`): `rules_core.cpp` (parser/evaluator/tokenizer)
- Standalone: `board.cpp`, `engine.cpp`, `cloudbook.cpp`, `main.cpp`

### Threading & Events
- All engine/cloudbook callbacks marshaled to main thread via `SDL_USEREVENT`
  - code 1 = bestmove, code 2 = engine info, code 3 = cloudbook result
- No cross-thread access to board or GUI state
- `SDL_PushEvent` to post events; delete payload if push fails

### Coordinate System
- Board: col 0-8 left→right, row 0-9 top→bottom
- Flip (上下翻转): row inverted, red on top
- Mirror (左右镜像): col → 8-col; applied before flip
- All coordinate functions accept `bool flipped, bool mirrored`

### Rules Engine
- Rule scripts in `xiangqi_rules.txt` (loaded via `/proc/self/exe` parent directory)
- Hot-reload: `reloadFromFile()` called every `checkGameRules()`
- Script syntax: `rule "name"` / `when <expr>` / `action <action>`
- Variables: `in_check`, `has_legal_moves`, `no_capture_count`, `move_count`, `current_side`, `last_captured`, `perpetual_check_count`, `position_repeat_count`
- Actions: `checkmate`, `draw`, `lose`, `win "msg"`, `warn "msg"`
- Check-capture cycle (一将一捉) rules removed
- Engine perpetual check auto-retract: position repeat > 6 times

### Engine Integration
- `isEngineTurn()` checks current side against `enginePlaysRed`/`enginePlaysBlack`
- `tryStartEngineMove()` guards: game over, viewing history, already thinking, analysis mode
- Engine violation detection: perpetual check with banned move list + searchmoves
- Analysis mode uses `sendStopForRestart()` to protect `m_thinking` flag

### Config
- INI file `xiangqi.ini` in executable directory, auto-load on start, auto-save on exit/engine load
- Keys: `engine_path`, `nnue_path`, `search_depth`, `search_time`, `threads`, `hash_size`, `use_cloud_book`, `engine_plays_red`, `engine_plays_black`, `flip_board`, `mirror_board`

### Build & Test
- Build: `cd build && cmake .. && make -j$(nproc)`
- Run: `./xiangqi` from `build/`
- Always compile and verify before committing

### Git Workflow
- Remote: `https://github.com/lijiahui-lim/xiangqi-gui.git`, branch `main`
- Commit messages in English, concise and descriptive
