#include "gui.h"
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <iostream>

namespace fs = std::filesystem;

GUI::GUI() = default;
GUI::~GUI() {
    saveConfig();
    m_engine.stop();
}

std::string GUI::detectEnginePath() {
    // 自动检测皮卡鱼引擎
    std::vector<std::string> candidates = {
        "/home/li/中国象棋/皮卡鱼 20260131/Linux/pikafish-bmi2",
        "/home/li/中国象棋/皮卡鱼 20260131/Linux/pikafish-avx2",
        "/home/li/中国象棋/皮卡鱼 20260131/Linux/pikafish-avx512",
        "/home/li/中国象棋/皮卡鱼 20260131/Linux/pikafish-sse41-popcnt",
    };
    for (auto& p : candidates)
        if (fs::exists(p)) return p;
    return "";
}

std::string GUI::detectNNUEPath() {
    std::string path = "/home/li/中国象棋/皮卡鱼 20260131/pikafish.nnue";
    if (fs::exists(path)) return path;
    return "";
}

std::string GUI::configFilePath() const {
    // 配置文件放在可执行文件同目录
    return "xiangqi.ini";
}

void GUI::loadConfig() {
    std::ifstream ifs(configFilePath());
    if (!ifs.is_open()) return;

    std::string line;
    while (std::getline(ifs, line)) {
        // 跳过空行和注释
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // trim
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());

        auto safeStoi = [](const std::string& s, int def = 0) -> int {
            try { return std::stoi(s); } catch (...) { return def; }
        };
        if (key == "engine_path") m_settings.enginePath = val;
        else if (key == "nnue_path") m_settings.nnuePath = val;
        else if (key == "search_depth") m_settings.searchDepth = safeStoi(val, 18);
        else if (key == "search_time") m_settings.searchTime = safeStoi(val, 5000);
        else if (key == "threads") m_settings.threads = safeStoi(val, 2);
        else if (key == "hash_size") m_settings.hashSize = safeStoi(val, 128);
        else if (key == "use_cloud_book") m_settings.useCloudBook = (val == "1" || val == "true");
        else if (key == "human_side") {
            // 兼容旧配置
            if (val == "black") { m_settings.enginePlaysRed = true; m_settings.enginePlaysBlack = false; }
            else { m_settings.enginePlaysRed = false; m_settings.enginePlaysBlack = true; }
        }
        else if (key == "engine_plays_red") m_settings.enginePlaysRed = (val == "1" || val == "true");
        else if (key == "engine_plays_black") m_settings.enginePlaysBlack = (val == "1" || val == "true");
        else if (key == "flip_board") m_settings.flipBoard = (val == "1" || val == "true");
        else if (key == "mirror_board") m_settings.mirrorBoard = (val == "1" || val == "true");
    }
}

void GUI::saveConfig() {
    std::ofstream ofs(configFilePath());
    if (!ofs.is_open()) return;

    ofs << "# 中国象棋 引擎配置文件\n";
    ofs << "engine_path=" << m_settings.enginePath << "\n";
    ofs << "nnue_path=" << m_settings.nnuePath << "\n";
    ofs << "search_depth=" << m_settings.searchDepth << "\n";
    ofs << "search_time=" << m_settings.searchTime << "\n";
    ofs << "threads=" << m_settings.threads << "\n";
    ofs << "hash_size=" << m_settings.hashSize << "\n";
    ofs << "use_cloud_book=" << (m_settings.useCloudBook ? "1" : "0") << "\n";
    ofs << "engine_plays_red=" << (m_settings.enginePlaysRed ? "1" : "0") << "\n";
    ofs << "engine_plays_black=" << (m_settings.enginePlaysBlack ? "1" : "0") << "\n";
    ofs << "flip_board=" << (m_settings.flipBoard ? "1" : "0") << "\n";
    ofs << "mirror_board=" << (m_settings.mirrorBoard ? "1" : "0") << "\n";
}

bool GUI::init() {
    if (!m_renderer.init(m_windowW, m_windowH))
        return false;

    // 先加载配置文件
    loadConfig();

    // 加载规则文件：优先从源码根目录（exe上级）加载，其次当前目录
    {
        fs::path exeDir = fs::canonical("/proc/self/exe").parent_path();
        fs::path rootRules = exeDir.parent_path() / "xiangqi_rules.txt";
        if (fs::exists(rootRules))
            m_rules.loadFromFile(rootRules.string());
        else
            m_rules.loadFromFile("xiangqi_rules.txt");
    }

    // 配置文件中没有引擎路径时自动检测
    if (m_settings.enginePath.empty() || !fs::exists(m_settings.enginePath)) {
        m_settings.enginePath = detectEnginePath();
    }
    if (m_settings.nnuePath.empty() || !fs::exists(m_settings.nnuePath)) {
        m_settings.nnuePath = detectNNUEPath();
    }

    initButtons();
    newGame();
    return true;
}

void GUI::initButtons() {
    auto lay = m_renderer.calcPanelLayout();
    int px = m_renderer.panelLeft();
    int pw = m_windowW - px - 15;

    // 按钮尺寸从 layout 推导（等比缩放）
    int bh = std::max(24, m_windowH / 23);
    int gap = std::max(4, bh / 5);
    int bw = std::max(50, (pw - gap * 2) / 3);

    // 从按钮区底部往上排列
    int by = lay.buttonsTop + lay.buttonsHeight - bh;

    m_buttons.clear();
    // 第1行（最底部）：新局 / 悔棋 / 翻转
    m_buttons.push_back({{px, by, bw, bh}, "新局", false});                         // 0
    m_buttons.push_back({{px + bw + gap, by, bw, bh}, "悔棋", false});              // 1
    m_buttons.push_back({{px + (bw + gap) * 2, by, bw, bh}, "翻转", false});        // 2
    by -= bh + gap;
    // 第2行：镜像 / 引擎执红 / 引擎执黑
    m_buttons.push_back({{px, by, bw, bh}, "镜像", false});                                                           // 3
    m_buttons.push_back({{px + bw + gap, by, bw, bh}, m_settings.enginePlaysRed ? "★执红" : "☆执红", false});          // 4
    m_buttons.push_back({{px + (bw + gap) * 2, by, bw, bh}, m_settings.enginePlaysBlack ? "★执黑" : "☆执黑", false}); // 5
    by -= bh + gap;
    // 第3行：设置 / 云库 / 停止
    m_buttons.push_back({{px, by, bw, bh}, "设置", false});              // 6
    m_buttons.push_back({{px + bw + gap, by, bw, bh}, m_settings.useCloudBook ? "★云库" : "☆云库", false});   // 7
    m_buttons.push_back({{px + (bw + gap) * 2, by, bw, bh}, "停止", false}); // 8
    by -= bh + gap;
    // 第4行：分析 / 加载引擎
    m_buttons.push_back({{px, by, bw, bh}, m_analysisMode ? "★分析" : "☆分析", false}); // 9
    m_buttons.push_back({{px + bw + gap, by, bw * 2 + gap, bh}, "加载引擎...", false}); // 10
    by -= bh + gap;
    // 第5行：棋谱导航 |< < > >|
    int navBw = std::max(30, (pw - gap * 3) / 4);
    m_buttons.push_back({{px, by, navBw, bh}, "|<", false});                        // 11
    m_buttons.push_back({{px + (navBw + gap), by, navBw, bh}, "<", false});         // 12
    m_buttons.push_back({{px + (navBw + gap) * 2, by, navBw, bh}, ">", false});     // 13
    m_buttons.push_back({{px + (navBw + gap) * 3, by, navBw, bh}, ">|", false});   // 14
    by -= bh + gap;
    // 第6行：导出 / 导入棋谱 / 编辑
    int pgnBw = std::max(40, (pw - gap * 2) / 3);
    m_buttons.push_back({{px, by, pgnBw, bh}, "导出棋谱", false});                      // 15
    m_buttons.push_back({{px + pgnBw + gap, by, pgnBw, bh}, "导入棋谱", false});          // 16
    m_buttons.push_back({{px + (pgnBw + gap) * 2, by, pgnBw, bh}, "编辑棋局", false});   // 17
}

void GUI::handleResize(int w, int h) {
    m_windowW = w;
    m_windowH = h;
    m_renderer.recalcLayout(w, h);
    initButtons();
}

void GUI::updateButtons(int mx, int my) {
    for (auto& btn : m_buttons) {
        SDL_Point p = {mx, my};
        btn.hovered = SDL_PointInRect(&p, &btn.rect);
    }
}

int GUI::hitButton(int x, int y) {
    SDL_Point p = {x, y};
    for (size_t i = 0; i < m_buttons.size(); i++)
        if (SDL_PointInRect(&p, &m_buttons[i].rect))
            return (int)i;
    return -1;
}

void GUI::update() {
    // 消息超时清除
    if (!m_message.empty() && m_uiState != UIState::GAME_OVER) {
        if (SDL_GetTicks() - m_messageTime > 3000)
            m_message.clear();
    }
}

void GUI::run() {
    m_running = true;
    SDL_Event ev;
    bool needRedraw = true;

    while (m_running) {
        bool hasEvent = false;
        if (m_engine.isThinking()) {
            // 引擎思考中：~20fps 刷新显示分析信息
            while (SDL_PollEvent(&ev)) {
                handleEvent(ev);
                hasEvent = true;
            }
            needRedraw = true;
            SDL_Delay(50);
        } else {
            // 非思考时阻塞等待，最长 200ms 唤醒一次（更新计时器显示）
            bool gotEvent = SDL_WaitEventTimeout(&ev, 200) != 0;
            if (gotEvent) {
                handleEvent(ev);
                hasEvent = true;
                while (SDL_PollEvent(&ev))
                    handleEvent(ev);
                needRedraw = true;
            }
            // 游戏计时器每 200ms 刷新一次
            if (m_currentMoveStartTime > 0)
                needRedraw = true;
        }

        update();

        // 消息超时清除需要重绘
        if (m_message.empty() && !m_engineInfoText.empty())
            needRedraw = true;

        if (needRedraw) {
            render();
            needRedraw = false;
        }
    }
}

void GUI::render() {
    m_renderer.beginFrame();

    const Board& dispBoard = displayBoard();

    // 绘制棋盘
    m_renderer.drawBoard(dispBoard, m_settings.flipBoard, m_settings.mirrorBoard);

    // 上一步高亮
    if (m_hasLastMove) {
        if (m_viewIndex >= 0) {
            // 浏览模式：高亮当前浏览步
            m_renderer.drawLastMove(m_moveHistory[m_viewIndex].move, m_settings.flipBoard, m_settings.mirrorBoard);
        } else {
            m_renderer.drawLastMove(m_lastMove, m_settings.flipBoard, m_settings.mirrorBoard);
        }
    }

    // 选中与合法走法（非浏览模式下）
    if (!isViewingHistory() && m_selRow >= 0 && m_selCol >= 0) {
        m_renderer.drawSelection(m_selRow, m_selCol, m_settings.flipBoard, m_settings.mirrorBoard);
        m_renderer.drawLegalMoves(m_legalMoves, m_settings.flipBoard, m_settings.mirrorBoard);
    }

    // PV 箭头（引擎思考或分析模式时，棋谱浏览/编辑模式下不显示）
    if (m_uiState != UIState::EDITING && !isViewingHistory()) {
        PVArrow a1 = m_pvArrow1;
        PVArrow a2 = m_pvArrow2;
        Side cur = dispBoard.currentSide();
        if (a1.valid)
            m_renderer.drawPVArrow(a1.move, a1.side, a1.side == cur,
                                   m_settings.flipBoard, m_settings.mirrorBoard);
        if (a2.valid)
            m_renderer.drawPVArrow(a2.move, a2.side, a2.side == cur,
                                   m_settings.flipBoard, m_settings.mirrorBoard);
    }

    // 侧边信息栏 或 编辑面板
    if (m_uiState == UIState::EDITING) {
        m_renderer.drawEditPalette(m_editPiece, m_editFirstSide);
    } else {
        m_renderer.drawSidePanel(m_engine.getEngineName(), m_engineInfoText,
                                  m_cloudMoves, m_cloudErrorText, dispBoard.currentSide(),
                                  m_engine.isThinking());
    }

    // 棋谱列表（编辑模式下不显示）
    if (m_uiState != UIState::EDITING) {
        auto lay = m_renderer.calcPanelLayout();
        std::vector<Renderer::MoveListEntry> entries;
        for (size_t i = 0; i < m_moveHistory.size(); i += 2) {
            Renderer::MoveListEntry e;
            e.roundNum = (int)(i / 2 + 1);
            e.redMove = m_moveHistory[i].chinese;
            if (i + 1 < m_moveHistory.size())
                e.blackMove = m_moveHistory[i + 1].chinese;
            entries.push_back(e);
        }
        int hl = m_viewIndex >= 0 ? m_viewIndex : (m_moveHistory.empty() ? -1 : (int)m_moveHistory.size() - 1);
        m_renderer.drawMoveList(entries, m_moveListScroll, hl, lay.moveListTop, lay.moveListHeight);
    }

    // 按钮（编辑模式下不显示）
    if (m_uiState != UIState::EDITING) {
        m_renderer.drawButtons(m_buttons);
    }

    // 计时器面板（棋盘左侧）
    {
        Side cur = dispBoard.currentSide();
        bool gameOver = (m_uiState == UIState::GAME_OVER);
        Uint32 elapsedMs = (!gameOver && m_currentMoveStartTime > 0)
            ? (SDL_GetTicks() - m_currentMoveStartTime) : 0;
        Uint32 redTotal   = m_redTimer.totalMs   + (cur == Side::RED   ? elapsedMs : 0);
        Uint32 blackTotal = m_blackTimer.totalMs + (cur == Side::BLACK ? elapsedMs : 0);
        Uint32 redLast   = (cur == Side::RED)   ? elapsedMs : m_redTimer.lastMoveMs;
        Uint32 blackLast = (cur == Side::BLACK) ? elapsedMs : m_blackTimer.lastMoveMs;
        m_renderer.drawClockPanel(redTotal,   redLast,
                                  blackTotal, blackLast,
                                  cur, m_settings.flipBoard,
                                  m_settings.mirrorBoard);
    }

    // 设置面板
    m_renderer.drawSettingsPanel("引擎设置", m_settingsLines,
                                 m_settingsScroll, m_uiState == UIState::SETTINGS);

    // 文件浏览器
    {
        Renderer::FileBrowserState fbState;
        fbState.currentDir = m_fbCurrentDir;
        fbState.entries = m_fbEntries;
        fbState.scrollOffset = m_fbScroll;
        fbState.selectedIndex = m_fbSelected;
        fbState.visible = (m_uiState == UIState::FILE_BROWSER);
        m_renderer.drawFileBrowser(fbState);
    }

    // 消息
    if (!m_message.empty())
        m_renderer.drawMessage(m_message);

    m_renderer.endFrame();
}

void GUI::queryCloudBook() {
    if (!m_settings.useCloudBook || !m_cloudBook.isEnabled()) return;
    m_cloudBook.query(m_board.toFEN(), [](const std::vector<CloudBookMove>& moves, const std::string& error) {
        SDL_Event ev; SDL_memset(&ev, 0, sizeof(ev));
        ev.type = SDL_USEREVENT; ev.user.code = 3;
        ev.user.data1 = new CloudBookEventData{moves, error};
        if (SDL_PushEvent(&ev) < 1) delete (CloudBookEventData*)ev.user.data1;
    });
}
