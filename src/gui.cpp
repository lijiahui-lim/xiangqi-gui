#include "gui.h"
#include <algorithm>
#include <sstream>
#include <filesystem>
#include <cstdio>
#include <iostream>
#include <cmath>

namespace fs = std::filesystem;

namespace {
// 编辑面板棋子顺序（索引 0-6 红，7-13 黑）
static const PieceType kPaletteOrder[] = {
    PieceType::KING, PieceType::ADVISOR, PieceType::BISHOP,
    PieceType::KNIGHT, PieceType::ROOK, PieceType::CANNON, PieceType::PAWN
};
} // namespace

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

        if (key == "engine_path") m_settings.enginePath = val;
        else if (key == "nnue_path") m_settings.nnuePath = val;
        else if (key == "search_depth") m_settings.searchDepth = std::stoi(val);
        else if (key == "search_time") m_settings.searchTime = std::stoi(val);
        else if (key == "threads") m_settings.threads = std::stoi(val);
        else if (key == "hash_size") m_settings.hashSize = std::stoi(val);
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

    // 加载规则文件
    m_rules.loadFromFile("xiangqi_rules.txt");

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

void GUI::newGame() {
    m_board.reset();
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_hasLastMove = false;
    m_moveHistory.clear();
    m_cloudMoves.clear();
    m_engineInfoText.clear();
    m_message.clear();
    m_viewIndex = -1;
    m_moveListScroll = 0;
    m_noCaptureCount = 0;
    m_analysisMode = false;
    m_ignoreNextBestMove = false;
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    // 计时器重置
    m_redTimer = {};
    m_blackTimer = {};
    startMoveTimer();
    m_rules.reloadFromFile(); // 每局重新读取规则文件
    m_viewBoard.reset();
    initButtons(); // 刷新按钮文字（分析模式状态等）

    // 启动引擎
    if (!m_settings.enginePath.empty() && !m_engine.isRunning()) {
        if (m_engine.start(m_settings.enginePath)) {
            applyEngineSettings();
        } else {
            m_message = "引擎启动失败!";
            m_messageTime = SDL_GetTicks();
        }
    }

    if (m_engine.isRunning()) {
        m_engine.sendNewGame();
        m_engine.sendIsReady();
    }

    // 如果引擎执红，引擎先走
    if (m_settings.enginePlaysRed && m_engine.isRunning()) {
        startEngineMove();
    }

    // 查询云库
    if (m_settings.useCloudBook && m_cloudBook.isEnabled()) {
        m_cloudBook.query(m_board.toFEN(), [this](const std::vector<CloudBookMove>& moves, const std::string& error) {
            onCloudBookResult(moves, error);
        });
    }
}

void GUI::applyEngineSettings() {
    if (!m_engine.isRunning()) return;

    // 等待引擎就绪
    int wait = 0;
    while (!m_engine.isReady() && wait < 50) {
        SDL_Delay(100);
        wait++;
    }

    m_engine.sendSetOption("Threads", std::to_string(m_settings.threads));
    m_engine.sendSetOption("Hash", std::to_string(m_settings.hashSize));
    if (!m_settings.nnuePath.empty()) {
        m_engine.sendSetOption("EvalFile", m_settings.nnuePath);
    }

    m_engine.setInfoCallback([this](const EngineInfo& info) {
        onEngineInfo(info);
    });
    m_engine.setBestMoveCallback([this](const std::string& uci) {
        // 通过SDL事件传递到主线程
        SDL_Event ev;
        ev.type = SDL_USEREVENT;
        ev.user.code = 1;
        // 存bestmove字符串
        char* data = new char[uci.size() + 1];
        strcpy(data, uci.c_str());
        ev.user.data1 = data;
        SDL_PushEvent(&ev);
    });
}

void GUI::startEngineMove() {
    if (!m_engine.isRunning()) return;

    // 构造走法列表
    std::vector<std::string> uciMoves;
    for (auto& mr : m_moveHistory) {
        uciMoves.push_back(Board::moveToUCI(mr.move));
    }

    // 使用初始 FEN 或编辑后的棋盘 FEN
    std::string fen;
    if (m_moveHistory.empty()) {
        fen = m_board.toFEN();
    } else {
        fen = m_moveHistory.front().fenBefore;
    }
    m_engine.sendPosition(fen, uciMoves);

    if (m_settings.searchTime > 0)
        m_engine.sendGo(0, m_settings.searchTime, 0, 0);
    else
        m_engine.sendGo(m_settings.searchDepth, 0, 0, 0);
}

bool GUI::isEngineTurn() const {
    Side cur = m_board.currentSide();
    return (cur == Side::RED && m_settings.enginePlaysRed) ||
           (cur == Side::BLACK && m_settings.enginePlaysBlack);
}

void GUI::tryStartEngineMove() {
    if (m_uiState == UIState::GAME_OVER) return;
    if (isViewingHistory()) return;
    if (m_engine.isThinking()) return;
    if (m_analysisMode) {
        startAnalysis();
        return;
    }
    if (isEngineTurn() && m_engine.isRunning()) {
        startEngineMove();
    }
}

void GUI::startAnalysis() {
    if (!m_engine.isRunning()) return;
    // 无论 m_thinking 状态如何，总是先 stop 再 go
    // 使用 sendStopForRestart 保证旧 bestmove 回复不会打断新的 go infinite
    m_engine.sendStopForRestart();

    // 构造走法列表
    std::vector<std::string> uciMoves;
    for (auto& mr : m_moveHistory)
        uciMoves.push_back(Board::moveToUCI(mr.move));

    std::string fen;
    if (m_moveHistory.empty())
        fen = m_board.toFEN();
    else
        fen = m_moveHistory.front().fenBefore;

    m_engine.sendPosition(fen, uciMoves);
    m_engine.sendGo(0, 0, 0, 0);  // go infinite
}

void GUI::toggleAnalysisMode() {
    m_analysisMode = !m_analysisMode;
    if (m_analysisMode) {
        // 进入分析模式：停止当前引擎走棋，开始无限分析
        if (m_engine.isThinking()) m_engine.sendStop();
        m_message = "分析模式: 开";
        m_messageTime = SDL_GetTicks();
        initButtons();
        // 稍后启动分析
        if (m_uiState != UIState::GAME_OVER && !isViewingHistory())
            startAnalysis();
    } else {
        // 退出分析模式
        if (m_engine.isThinking()) {
            // 必须先设标志，再发 stop，防止引擎回复 bestmove 时错误执行走棋
            m_ignoreNextBestMove = true;
            m_engine.sendStop();
        }
        m_engineInfoText.clear();
        m_pvArrow1.valid = false;
        m_pvArrow2.valid = false;
        m_message = "分析模式: 关";
        m_messageTime = SDL_GetTicks();
        initButtons();
        // 如果轮到引擎走，恢复引擎走棋
        tryStartEngineMove();
    }
}

void GUI::onEngineBestMove(const std::string& uci) {
    // 忽略由停止分析产生的 bestmove
    if (m_ignoreNextBestMove) {
        m_ignoreNextBestMove = false;
        return;
    }
    // 分析模式下忽略bestmove（不自动走棋）
    if (m_analysisMode) return;

    if (uci.empty() || uci == "(none)") return;

    Move move = Board::uciToMove(uci, m_board);
    if (move.fromRow < 0) return;

    // 验证走法
    Piece p = m_board.get(move.fromRow, move.fromCol);
    if (p.empty() || p.side != m_board.currentSide()) return;

    // 记录本步用时
    stopMoveTimerAndRecord(m_board.currentSide());

    move.captured = m_board.get(move.toRow, move.toCol);

    std::string uciStr = uci;
    std::string chinese = uciToChineseNotation(uciStr, m_board);
    std::string fenBefore = m_board.toFEN();
    m_moveHistory.push_back({move, m_board.currentSide(), uciStr, chinese, fenBefore});
    m_board.makeMove(move);
    m_board.switchSide();
    m_lastMove = move;
    m_hasLastMove = true;
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();

    // 更新无吃子计数
    if (!move.captured.empty())
        m_noCaptureCount = 0;
    else
        m_noCaptureCount++;

    // 启动下一方计时器
    startMoveTimer();

    // 检查规则
    if (!checkGameRules()) return;

    // 查询云库
    if (m_settings.useCloudBook && m_cloudBook.isEnabled()) {
        m_cloudBook.query(m_board.toFEN(), [this](const std::vector<CloudBookMove>& moves, const std::string& error) {
            onCloudBookResult(moves, error);
        });
    }

    // 若下一方也由引擎控制，继续走
    // 走棋后清除旧箭头
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    tryStartEngineMove();
}

void GUI::onEngineInfo(const EngineInfo& info) {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    std::ostringstream oss;
    oss << "深度: " << info.depth;
    if (info.isMate)
        oss << "  杀棋: " << info.mateIn << "步";
    else
        oss << "  评分: " << info.score / 100.0;
    oss << "\n节点: " << info.nodes;
    if (info.nps > 0) oss << "  速度: " << info.nps / 1000 << "k/s";
    if (!info.pv.empty()) oss << "\n路线: " << pvToReadable(info.pv);
    m_engineInfoText = oss.str();

    // 解析 PV 前两步用于查棋箭头显示
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    if (!info.pv.empty()) {
        Board tmpBoard;
        tmpBoard.fromFEN(m_board.toFEN());
        std::istringstream pvss(info.pv);
        std::string uci;
        int count = 0;
        while (pvss >> uci && count < 2) {
            Move mv = Board::uciToMove(uci, tmpBoard);
            if (mv.fromRow < 0) break;
            mv.captured = tmpBoard.get(mv.toRow, mv.toCol);
            if (count == 0)
                m_pvArrow1 = {true, mv, tmpBoard.currentSide()};
            else
                m_pvArrow2 = {true, mv, tmpBoard.currentSide()};
            tmpBoard.makeMove(mv);
            tmpBoard.switchSide();
            count++;
        }
    }
}

std::string GUI::pvToReadable(const std::string& pv) const {
    // 在临时棋盘上模拟走法，逐步转换
    Board tmpBoard;
    tmpBoard.fromFEN(m_board.toFEN());

    std::istringstream ss(pv);
    std::string uci;
    std::string result;
    int count = 0;
    while (ss >> uci && count < 8) {
        std::string readable = uciToChineseNotation(uci, tmpBoard);
        if (!result.empty()) result += " ";
        result += readable;

        // 在临时棋盘上执行这步
        Move m = Board::uciToMove(uci, tmpBoard);
        if (m.fromRow >= 0) {
            m.captured = tmpBoard.get(m.toRow, m.toCol);
            tmpBoard.makeMove(m);
            tmpBoard.switchSide();
        }
        count++;
    }
    return result;
}

void GUI::onCloudBookResult(const std::vector<CloudBookMove>& moves, const std::string& error) {
    std::lock_guard<std::mutex> lock(m_infoMutex);
    if (!error.empty()) {
        m_cloudErrorText = error;
        m_cloudMoves.clear();
        return;
    }
    m_cloudMoves = moves;
    m_cloudErrorText.clear();
    // 把UCI走法转换为中文记法存入note字段
    for (auto& cm : m_cloudMoves) {
        cm.note = uciToChineseNotation(cm.move, m_board);
    }
}

void GUI::makeHumanMove(int fromR, int fromC, int toR, int toC) {
    Piece p = m_board.get(fromR, fromC);
    if (p.empty()) return;

    // 分析模式下先停止当前分析（startAnalysis 内部也会 stop，此处可省，但保留以快速响应UI）
    if (m_analysisMode) {
        m_engine.sendStopForRestart();
    }

    // 如果在浏览历史，回到实时局面
    if (m_viewIndex >= 0) {
        m_viewIndex = -1;
    }

    // 记录本步用时
    stopMoveTimerAndRecord(m_board.currentSide());

    Move move = {fromR, fromC, toR, toC, m_board.get(toR, toC)};
    std::string uci = Board::moveToUCI(move);
    std::string chinese = uciToChineseNotation(uci, m_board);
    std::string fenBefore = m_board.toFEN();
    m_moveHistory.push_back({move, m_board.currentSide(), uci, chinese, fenBefore});
    m_board.makeMove(move);
    m_board.switchSide();
    m_lastMove = move;
    m_hasLastMove = true;
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();

    // 更新无吃子计数
    if (!move.captured.empty())
        m_noCaptureCount = 0;
    else
        m_noCaptureCount++;

    // 启动下一方计时器
    startMoveTimer();

    // 检查规则
    if (!checkGameRules()) return;

    // 云库查询
    if (m_settings.useCloudBook && m_cloudBook.isEnabled()) {
        m_cloudBook.query(m_board.toFEN(), [this](const std::vector<CloudBookMove>& moves, const std::string& error) {
            onCloudBookResult(moves, error);
        });
    }

    // 引擎走棋
    if (m_analysisMode) {
        startAnalysis();
    } else if (m_gameMode == GameMode::HUMAN_VS_ENGINE) {
        tryStartEngineMove();
    }
}

bool GUI::checkGameRules() {
    // 动态读取：每次判定前都重载规则脚本
    m_rules.reloadFromFile();

    bool hasLegalMove = false;
    for (int r = 0; r < Board::ROWS && !hasLegalMove; r++) {
        for (int c = 0; c < Board::COLS && !hasLegalMove; c++) {
            auto piece = m_board.get(r, c);
            if (piece.side == m_board.currentSide()) {
                auto moves = m_board.getLegalMoves(r, c);
                if (!moves.empty()) hasLegalMove = true;
            }
        }
    }

    bool inCheck = m_board.isInCheck(m_board.currentSide());
    GameState st;
    st.inCheck = inCheck;
    st.hasLegalMoves = hasLegalMove;
    st.noCaptureCount = m_noCaptureCount;
    st.moveCount = (int)m_moveHistory.size();
    st.currentSide = (m_board.currentSide() == Side::RED) ? 0 : 1;
    st.lastCaptured = (!m_moveHistory.empty() && !m_moveHistory.back().move.captured.empty());

    RuleResult rr = m_rules.evaluate(st);
    switch (rr.action) {
        case RuleAction::CHECKMATE:
            m_message = rr.message.empty()
                ? ((m_board.currentSide() == Side::RED) ? "红方被将死! 黑方胜!" : "黑方被将死! 红方胜!")
                : rr.message;
            m_messageTime = SDL_GetTicks();
            m_uiState = UIState::GAME_OVER;
            return false;
        case RuleAction::DRAW:
            m_message = rr.message.empty() ? "和棋!" : rr.message;
            m_messageTime = SDL_GetTicks();
            m_uiState = UIState::GAME_OVER;
            return false;
        case RuleAction::LOSE:
            m_message = rr.message.empty()
                ? ((m_board.currentSide() == Side::RED) ? "红方负!" : "黑方负!")
                : rr.message;
            m_messageTime = SDL_GetTicks();
            m_uiState = UIState::GAME_OVER;
            return false;
        case RuleAction::WARN:
            if (!rr.message.empty()) {
                m_message = rr.message;
                m_messageTime = SDL_GetTicks();
            }
            break;
        case RuleAction::NONE:
        default:
            break;
    }

    // 兜底：若规则文件未写终局规则，保持可玩性
    if (!hasLegalMove) {
        m_message = (m_board.currentSide() == Side::RED) ? "黑方胜!" : "红方胜!";
        m_messageTime = SDL_GetTicks();
        m_uiState = UIState::GAME_OVER;
        return false;
    }

    if (inCheck && rr.action == RuleAction::NONE) {
        m_message = "将军!";
        m_messageTime = SDL_GetTicks();
    }

    return true;
}

void GUI::undoLastMove() {
    if (m_analysisMode) {
        m_engine.sendStopForRestart();  // 分析模式：重启前 stop
    } else if (m_engine.isThinking()) {
        m_engine.sendStop();
    }

    // 如果在浏览历史，先回到实时
    if (m_viewIndex >= 0) {
        m_viewIndex = -1;
    }

    // 人机模式下悔两步（人+机），分析模式下只悔一步
    int undoCount = (m_gameMode == GameMode::HUMAN_VS_ENGINE && !m_analysisMode) ? 2 : 1;
    for (int i = 0; i < undoCount && !m_moveHistory.empty(); i++) {
        auto& rec = m_moveHistory.back();
        m_board.undoMove(rec.move);
        m_board.switchSide();
        m_moveHistory.pop_back();
    }

    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_hasLastMove = !m_moveHistory.empty();
    if (m_hasLastMove)
        m_lastMove = m_moveHistory.back().move;

    // 重新计算无吃子步数
    m_noCaptureCount = 0;
    for (int i = (int)m_moveHistory.size() - 1; i >= 0; i--) {
        if (!m_moveHistory[i].move.captured.empty()) break;
        m_noCaptureCount++;
    }

    if (m_uiState == UIState::GAME_OVER)
        m_uiState = UIState::NORMAL;

    // 查询云库
    if (m_settings.useCloudBook && m_cloudBook.isEnabled()) {
        m_cloudBook.query(m_board.toFEN(), [this](const std::vector<CloudBookMove>& moves, const std::string& error) {
            onCloudBookResult(moves, error);
        });
    }

    // 分析模式下重新分析
    if (m_analysisMode) {
        startAnalysis();
    }
}

void GUI::handleResize(int w, int h) {
    m_windowW = w;
    m_windowH = h;
    m_renderer.recalcLayout(w, h);
    initButtons();
}

void GUI::loadEngine(const std::string& path) {
    if (path.empty()) return;
    if (m_engine.isRunning()) {
        m_engine.sendStop();
        m_engine.stop();
    }
    m_settings.enginePath = path;

    // 自动查找同目录下的 nnue 文件
    fs::path engineDir = fs::path(path).parent_path().parent_path();
    for (auto& entry : fs::directory_iterator(engineDir)) {
        if (entry.path().extension() == ".nnue") {
            m_settings.nnuePath = entry.path().string();
            break;
        }
    }

    if (m_engine.start(path)) {
        applyEngineSettings();
        m_engine.sendNewGame();
        m_engine.sendIsReady();
        m_message = "引擎已加载: " + fs::path(path).filename().string();
    } else {
        m_message = "引擎加载失败!";
    }
    m_messageTime = SDL_GetTicks();

    // 加载引擎后保存配置
    saveConfig();
}

void GUI::openEngineFileDialog() {
    // 打开内建文件浏览器
    m_uiState = UIState::FILE_BROWSER;
    m_fbScroll = 0;
    m_fbSelected = -1;
    // 从引擎所在目录开始，或从 /home/li/中国象棋 开始
    std::string startDir = "/home/li/中国象棋";
    if (!m_settings.enginePath.empty()) {
        startDir = fs::path(m_settings.enginePath).parent_path().string();
    }
    fbNavigate(startDir);
}

void GUI::fbNavigate(const std::string& dir) {
    m_fbCurrentDir = dir;
    m_fbEntries.clear();
    m_fbScroll = 0;
    m_fbSelected = -1;

    // 返回上级
    if (dir != "/") {
        m_fbEntries.push_back("../");
    }

    try {
        std::vector<std::string> dirs, files;
        for (auto& entry : fs::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue; // 隐藏文件
            if (entry.is_directory()) {
                dirs.push_back(name + "/");
            } else {
                // 只显示可执行文件和目录
                auto perms = entry.status().permissions();
                bool isExec = (perms & fs::perms::owner_exec) != fs::perms::none ||
                              (perms & fs::perms::group_exec) != fs::perms::none ||
                              (perms & fs::perms::others_exec) != fs::perms::none;
                if (isExec || name.find("pikafish") != std::string::npos ||
                    name.find("engine") != std::string::npos) {
                    files.push_back(name);
                }
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        m_fbEntries.insert(m_fbEntries.end(), dirs.begin(), dirs.end());
        m_fbEntries.insert(m_fbEntries.end(), files.begin(), files.end());
    } catch (...) {
        m_fbEntries.push_back("(无法读取目录)");
    }
}

void GUI::fbHandleClick(int x, int y) {
    Renderer::FileBrowserState state;
    state.currentDir = m_fbCurrentDir;
    state.entries = m_fbEntries;
    state.scrollOffset = m_fbScroll;
    state.selectedIndex = m_fbSelected;
    state.visible = true;

    int idx = m_renderer.fileBrowserHitTest(x, y, state);
    if (idx < 0) {
        // 点击面板外关闭
        int pw = std::min(m_windowW - 40, 600);
        int ph = std::min(m_windowH - 40, 500);
        int px = (m_windowW - pw) / 2;
        int py = (m_windowH - ph) / 2;
        if (x < px || x > px + pw || y < py || y > py + ph) {
            m_uiState = UIState::NORMAL;
        }
        return;
    }

    std::string entry = m_fbEntries[idx];
    if (entry == "../") {
        // 上级目录
        fs::path parent = fs::path(m_fbCurrentDir).parent_path();
        fbNavigate(parent.string());
    } else if (!entry.empty() && entry.back() == '/') {
        // 进入子目录
        std::string sub = entry.substr(0, entry.size() - 1);
        fbNavigate((fs::path(m_fbCurrentDir) / sub).string());
    } else if (entry.find("(") == std::string::npos) {
        // 选择文件
        std::string fullPath = (fs::path(m_fbCurrentDir) / entry).string();
        m_uiState = UIState::NORMAL;
        loadEngine(fullPath);
    }
}

void GUI::fbHandleScroll(int dy) {
    m_fbScroll -= dy;
    m_fbScroll = std::max(0, std::min(m_fbScroll, (int)m_fbEntries.size() - 5));
}

// UCI走法 -> 中文棋谱记法
std::string GUI::uciToChineseNotation(const std::string& uci, const Board& board) {
    if (uci.size() < 4) return uci;

    int fc = uci[0] - 'a', fr = uci[1] - '0';
    int tc = uci[2] - 'a', tr = uci[3] - '0';

    Piece piece = board.get(fr, fc);
    if (piece.empty()) return uci;

    Side side = piece.side;

    // 棋子名
    const char* redNames[]   = {"", "帅", "仕", "相", "马", "车", "炮", "兵"};
    const char* blackNames[] = {"", "将", "士", "象", "马", "车", "炮", "卒"};
    const char* name = (side == Side::RED) ? redNames[(int)piece.type] : blackNames[(int)piece.type];

    // 红方数字(中文), 黑方数字(阿拉伯)
    const char* redNums[]   = {"", "一", "二", "三", "四", "五", "六", "七", "八", "九"};
    const char* blackNums[] = {"", "1", "2", "3", "4", "5", "6", "7", "8", "9"};

    auto colNum = [&](int col) -> std::string {
        if (side == Side::RED) {
            int n = 9 - col; // 红方从右到左 1-9
            return (n >= 1 && n <= 9) ? redNums[n] : "?";
        } else {
            int n = col + 1; // 黑方从左到右 1-9
            return (n >= 1 && n <= 9) ? blackNums[n] : "?";
        }
    };

    auto stepNum = [&](int steps) -> std::string {
        int n = std::abs(steps);
        if (side == Side::RED) {
            return (n >= 1 && n <= 9) ? redNums[n] : std::to_string(n);
        } else {
            return (n >= 1 && n <= 9) ? blackNums[n] : std::to_string(n);
        }
    };

    std::string result = name;
    result += colNum(fc);

    // 判断进/退/平
    if (fr == tr) {
        // 平
        result += "平";
        result += colNum(tc);
    } else {
        bool forward = (side == Side::RED) ? (tr > fr) : (tr < fr);
        result += forward ? "进" : "退";

        // 马、象、士：用目标列号; 车、炮、兵、帅：用步数
        if (piece.type == PieceType::KNIGHT || piece.type == PieceType::BISHOP ||
            piece.type == PieceType::ADVISOR) {
            result += colNum(tc);
        } else {
            result += stepNum(tr - fr);
        }
    }

    return result;
}

void GUI::handleMouseDown(int x, int y) {
    // 文件浏览器模式
    if (m_uiState == UIState::FILE_BROWSER) {
        fbHandleClick(x, y);
        return;
    }

    // 检查按钮
    int btnIdx = hitButton(x, y);
    if (btnIdx >= 0) {
        switch (btnIdx) {
            case 0: // 新局
                newGame();
                break;
            case 1: // 悔棋
                undoLastMove();
                break;
            case 2: // 翻转
                m_settings.flipBoard = !m_settings.flipBoard;
                break;
            case 3: // 镜像
                m_settings.mirrorBoard = !m_settings.mirrorBoard;
                break;
            case 4: // 引擎执红
                m_settings.enginePlaysRed = !m_settings.enginePlaysRed;
                m_message = m_settings.enginePlaysRed ? "引擎执红: 开" : "引擎执红: 关";
                m_messageTime = SDL_GetTicks();
                initButtons(); // 刷新按钮文字
                tryStartEngineMove();
                break;
            case 5: // 引擎执黑
                m_settings.enginePlaysBlack = !m_settings.enginePlaysBlack;
                m_message = m_settings.enginePlaysBlack ? "引擎执黑: 开" : "引擎执黑: 关";
                m_messageTime = SDL_GetTicks();
                initButtons(); // 刷新按钮文字
                tryStartEngineMove();
                break;
            case 6: // 设置
                if (m_uiState == UIState::SETTINGS) {
                    m_uiState = UIState::NORMAL;
                } else {
                    m_uiState = UIState::SETTINGS;
                    m_settingsScroll = 0;
                    m_settingsLines.clear();
                    m_settingsLines.push_back("引擎: " + m_settings.enginePath);
                    m_settingsLines.push_back("权重: " + m_settings.nnuePath);
                    m_settingsLines.push_back("搜索深度: " + std::to_string(m_settings.searchDepth));
                    m_settingsLines.push_back("搜索时间: " + std::to_string(m_settings.searchTime) + "ms");
                    m_settingsLines.push_back("线程数: " + std::to_string(m_settings.threads));
                    m_settingsLines.push_back("哈希表: " + std::to_string(m_settings.hashSize) + "MB");
                    m_settingsLines.push_back("云库: " + std::string(m_settings.useCloudBook ? "开启" : "关闭"));
                    m_settingsLines.push_back("引擎执红: " + std::string(m_settings.enginePlaysRed ? "开" : "关"));
                    m_settingsLines.push_back("引擎执黑: " + std::string(m_settings.enginePlaysBlack ? "开" : "关"));
                    m_settingsLines.push_back("");
                    m_settingsLines.push_back("快捷键:");
                    m_settingsLines.push_back("  N - 新对局");
                    m_settingsLines.push_back("  U - 悔棋");
                    m_settingsLines.push_back("  F - 翻转棋盘");
                    m_settingsLines.push_back("  M - 镜像棋盘");
                    m_settingsLines.push_back("  S - 停止引擎思考");
                    m_settingsLines.push_back("  A - 开关分析模式");
                    m_settingsLines.push_back("  C - 开关云库");
                    m_settingsLines.push_back("  ESC - 关闭设置");
                    m_settingsLines.push_back("  1-9 - 设置搜索深度(x2)");
                    m_settingsLines.push_back("  +/- - 调整线程数");
                    m_settingsLines.push_back("");

                    // 显示引擎选项
                    auto& opts = m_engine.getOptions();
                    if (!opts.empty()) {
                        m_settingsLines.push_back("引擎选项:");
                        for (auto& opt : opts) {
                            std::string line = "  " + opt.name + " (" + opt.type + ")";
                            if (!opt.value.empty()) line += " = " + opt.value;
                            if (opt.type == "spin") {
                                line += " [" + std::to_string(opt.min) + "-" + std::to_string(opt.max) + "]";
                            }
                            m_settingsLines.push_back(line);
                        }
                    }
                }
                break;
            case 7: // 云库
                m_settings.useCloudBook = !m_settings.useCloudBook;
                m_cloudBook.setEnabled(m_settings.useCloudBook);
                initButtons(); // 刷新按钮文字
                if (m_settings.useCloudBook) {
                    m_cloudBook.query(m_board.toFEN(), [this](const std::vector<CloudBookMove>& moves, const std::string& error) {
                        onCloudBookResult(moves, error);
                    });
                    m_message = "云库已开启";
                } else {
                    m_message = "云库已关闭";
                    m_cloudMoves.clear();
                    m_cloudErrorText.clear();
                }
                m_messageTime = SDL_GetTicks();
                break;
            case 8: // 停止
                if (m_engine.isThinking()) {
                    m_engine.sendStop();
                    m_message = "已停止思考";
                    m_messageTime = SDL_GetTicks();
                }
                break;
            case 9: // 分析
                toggleAnalysisMode();
                break;
            case 10: // 加载引擎
                openEngineFileDialog();
                break;
            case 11: // 首步
                if (!m_moveHistory.empty()) navigateTo(0);
                break;
            case 12: // 后退
                navigateBackward();
                break;
            case 13: // 前进
                navigateForward();
                break;
            case 14: // 末步
                navigateTo(-1);
                break;
            case 15: // 导出棋谱
                openPGNExportDialog();
                break;
            case 16: // 导入棋谱
                openPGNImportDialog();
                break;
            case 17: // 编辑棋局
                if (m_uiState == UIState::EDITING) {
                    exitEditMode();
                } else {
                    enterEditMode();
                }
                break;
        }
        return;
    }

    // 编辑模式
    if (m_uiState == UIState::EDITING) {
        // 先检查编辑面板点击
        int hit = editPaletteHitTest(x, y);
        if (hit >= 0) {
            if (hit <= 13) {
                // 选择棋子
                Side s = (hit < 7) ? Side::RED : Side::BLACK;
                m_editPiece.type = kPaletteOrder[hit < 7 ? hit : hit - 7];
                m_editPiece.side = s;
                return;
            } else if (hit == 14) {
                m_editPiece = {};  // 橡皮擦
                return;
            } else if (hit == 15) {
                // 清空棋盘
                for (int r = 0; r < Board::ROWS; r++)
                    for (int c = 0; c < Board::COLS; c++)
                        m_board.set(r, c, Piece{});
                return;
            } else if (hit == 16) {
                // 还原初始局面
                m_board.reset();
                return;
            } else if (hit == 17) {
                exitEditMode();
                return;
            } else if (hit == 18) {
                m_editFirstSide = Side::RED;
                return;
            } else if (hit == 19) {
                m_editFirstSide = Side::BLACK;
                return;
            }
        }
        // 棋盘点击：放置棋子
        int row, col;
        if (m_renderer.screenToBoard(x, y, row, col, m_settings.flipBoard, m_settings.mirrorBoard)) {
            handleEditBoardClick(row, col, false);
        }
        return;
    }

    // 设置面板下不响应棋盘点击
    if (m_uiState == UIState::SETTINGS) {
        m_uiState = UIState::NORMAL;
        return;
    }

    // 引擎思考中不允许操作棋盘（分析模式除外）
    if (m_engine.isThinking() && !m_analysisMode) return;

    // 棋谱点击（浏览历史时也允许点击跳转）
    int moveIdx = moveListHitTest(x, y);
    if (moveIdx >= 0 && moveIdx < (int)m_moveHistory.size()) {
        navigateTo(moveIdx);
        return;
    }

    // 浏览历史时不允许操作棋盘
    if (isViewingHistory()) return;

    // 棋盘点击
    int row, col;
    if (!m_renderer.screenToBoard(x, y, row, col, m_settings.flipBoard, m_settings.mirrorBoard))
        return;

    Piece clicked = m_board.get(row, col);

    if (m_selRow >= 0 && m_selCol >= 0) {
        // 已选中棋子
        if (clicked.side == m_board.currentSide()) {
            // 换选
            m_selRow = row;
            m_selCol = col;
            m_legalMoves = m_board.getLegalMoves(row, col);
        } else if (m_board.isLegalMove(m_selRow, m_selCol, row, col)) {
            // 走棋
            makeHumanMove(m_selRow, m_selCol, row, col);
        } else {
            // 非法走法，清除选择
            m_selRow = m_selCol = -1;
            m_legalMoves.clear();
        }
    } else {
        // 未选中，选子
        if (!clicked.empty() && clicked.side == m_board.currentSide()) {
            // 人机模式下不能走引擎控制的棋（分析模式除外）
            if (m_gameMode == GameMode::HUMAN_VS_ENGINE && !m_analysisMode && isEngineTurn())
                return;
            m_selRow = row;
            m_selCol = col;
            m_legalMoves = m_board.getLegalMoves(row, col);
        }
    }
}

void GUI::handleMouseUp(int, int) {}

void GUI::handleMouseMove(int x, int y) {
    updateButtons(x, y);
}

void GUI::handleKeyDown(SDL_Keycode key) {
    switch (key) {
        case SDLK_F11: {
            SDL_Window* win = m_renderer.window();
            Uint32 flags = SDL_GetWindowFlags(win);
            if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP)
                SDL_SetWindowFullscreen(win, 0);
            else
                SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
            break;
        }
        case SDLK_n: newGame(); break;
        case SDLK_u: undoLastMove(); break;
        case SDLK_f: m_settings.flipBoard = !m_settings.flipBoard; break;
        case SDLK_m: m_settings.mirrorBoard = !m_settings.mirrorBoard; break;
        case SDLK_s:
            if (m_engine.isThinking()) m_engine.sendStop();
            break;
        case SDLK_a:
            toggleAnalysisMode();
            break;
        case SDLK_c:
            m_settings.useCloudBook = !m_settings.useCloudBook;
            m_cloudBook.setEnabled(m_settings.useCloudBook);
            break;
        case SDLK_ESCAPE:
            if (m_uiState == UIState::SETTINGS || m_uiState == UIState::FILE_BROWSER)
                m_uiState = UIState::NORMAL;
            else if (m_viewIndex >= 0)
                m_viewIndex = -1; // ESC退出浏览模式
            break;
        case SDLK_LEFT:
            navigateBackward();
            break;
        case SDLK_RIGHT:
            navigateForward();
            break;
        case SDLK_HOME:
            if (!m_moveHistory.empty()) navigateTo(0);
            break;
        case SDLK_END:
            navigateTo(-1);
            break;
        case SDLK_PLUS: case SDLK_EQUALS:
            m_settings.threads = std::min(m_settings.threads + 1, 64);
            if (m_engine.isRunning())
                m_engine.sendSetOption("Threads", std::to_string(m_settings.threads));
            m_message = "线程数: " + std::to_string(m_settings.threads);
            m_messageTime = SDL_GetTicks();
            break;
        case SDLK_MINUS:
            m_settings.threads = std::max(m_settings.threads - 1, 1);
            if (m_engine.isRunning())
                m_engine.sendSetOption("Threads", std::to_string(m_settings.threads));
            m_message = "线程数: " + std::to_string(m_settings.threads);
            m_messageTime = SDL_GetTicks();
            break;
        default:
            if (key >= SDLK_1 && key <= SDLK_9) {
                m_settings.searchDepth = (key - SDLK_1 + 1) * 2;
                m_message = "搜索深度: " + std::to_string(m_settings.searchDepth);
                m_messageTime = SDL_GetTicks();
            }
            break;
    }
}

void GUI::handleEvent(SDL_Event& ev) {
    switch (ev.type) {
        case SDL_QUIT:
            m_running = false;
            break;
        case SDL_WINDOWEVENT:
            if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                handleResize(ev.window.data1, ev.window.data2);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.button == SDL_BUTTON_LEFT)
                handleMouseDown(ev.button.x, ev.button.y);
            else if (ev.button.button == SDL_BUTTON_RIGHT && m_uiState == UIState::EDITING) {
                // 右键：删除棋子
                int row, col;
                if (m_renderer.screenToBoard(ev.button.x, ev.button.y, row, col,
                                              m_settings.flipBoard, m_settings.mirrorBoard)) {
                    handleEditBoardClick(row, col, true);
                }
            }
            break;
        case SDL_MOUSEBUTTONUP:
            handleMouseUp(ev.button.x, ev.button.y);
            break;
        case SDL_MOUSEMOTION:
            handleMouseMove(ev.motion.x, ev.motion.y);
            break;
        case SDL_KEYDOWN:
            handleKeyDown(ev.key.keysym.sym);
            break;
        case SDL_MOUSEWHEEL:
            if (m_uiState == UIState::FILE_BROWSER) {
                fbHandleScroll(ev.wheel.y);
            } else if (m_uiState == UIState::SETTINGS) {
                m_settingsScroll -= ev.wheel.y * 20;
                m_settingsScroll = std::max(0, m_settingsScroll);
            } else {
                // 检查鼠标是否在面板区域（棋谱列表）
                int mx, my;
                SDL_GetMouseState(&mx, &my);
                if (mx >= m_renderer.panelLeft()) {
                    moveListHandleScroll(ev.wheel.y);
                }
            }
            break;
        case SDL_USEREVENT:
            if (ev.user.code == 1 && ev.user.data1) {
                std::string bestmove((char*)ev.user.data1);
                delete[] (char*)ev.user.data1;
                onEngineBestMove(bestmove);
            }
            break;
    }
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
        PVArrow a1, a2;
        {
            std::lock_guard<std::mutex> lock(m_infoMutex);
            a1 = m_pvArrow1;
            a2 = m_pvArrow2;
        }
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
        std::lock_guard<std::mutex> lock(m_infoMutex);
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
        // 游戏结束后时钟冻结，不再累加
        bool gameOver = (m_uiState == UIState::GAME_OVER);
        Uint32 elapsedMs = (!gameOver && m_currentMoveStartTime > 0)
            ? (SDL_GetTicks() - m_currentMoveStartTime) : 0;
        Uint32 redTotal   = m_redTimer.totalMs   + (cur == Side::RED   ? elapsedMs : 0);
        Uint32 blackTotal = m_blackTimer.totalMs + (cur == Side::BLACK ? elapsedMs : 0);
        // 步时：当前走棋方实时累加，另一方显示上一步记录值
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

// ========== 棋谱功能 ==========

const Board& GUI::displayBoard() const {
    if (m_viewIndex >= 0) return m_viewBoard;
    return m_board;
}

void GUI::navigateTo(int index) {
    if (m_moveHistory.empty()) return;

    if (index < 0) {
        // 回到实时局面
        m_viewIndex = -1;
        return;
    }

    index = std::min(index, (int)m_moveHistory.size() - 1);
    m_viewIndex = index;

    // 从初始局面重放到index
    m_viewBoard.reset();
    for (int i = 0; i <= index; i++) {
        auto& rec = m_moveHistory[i];
        m_viewBoard.makeMove(rec.move);
        m_viewBoard.switchSide();
    }
}

void GUI::navigateForward() {
    if (m_moveHistory.empty()) return;
    if (m_viewIndex < 0) return; // 已在实时局面
    if (m_viewIndex >= (int)m_moveHistory.size() - 1) {
        m_viewIndex = -1; // 回到实时
        return;
    }
    navigateTo(m_viewIndex + 1);
}

void GUI::navigateBackward() {
    if (m_moveHistory.empty()) return;
    if (m_viewIndex < 0) {
        // 从实时回退到最后一步
        navigateTo((int)m_moveHistory.size() - 1);
        return;
    }
    if (m_viewIndex == 0) return; // 已在最开始
    navigateTo(m_viewIndex - 1);
}

void GUI::exportPGN(const std::string& path) {
    std::ofstream ofs(path);
    if (!ofs.is_open()) {
        m_message = "导出失败: 无法创建文件";
        m_messageTime = SDL_GetTicks();
        return;
    }

    // PGN 头
    ofs << "[Game \"Chinese Chess\"]\n";
    ofs << "[Date \"" << __DATE__ << "\"]\n";
    ofs << "[Red \"Player\"]\n";
    ofs << "[Black \"Pikafish\"]\n";
    ofs << "[Result \"*\"]\n";
    if (!m_moveHistory.empty()) {
        ofs << "[FEN \"" << m_moveHistory[0].fenBefore << "\"]\n";
    }
    ofs << "\n";

    // 走法
    for (size_t i = 0; i < m_moveHistory.size(); i++) {
        if (i % 2 == 0) {
            ofs << (i / 2 + 1) << ". ";
        }
        ofs << m_moveHistory[i].uci;
        // 中文注释
        ofs << " {" << m_moveHistory[i].chinese << "}";
        if (i % 2 == 1) ofs << "\n";
        else ofs << " ";
    }
    ofs << " *\n";
    ofs.close();

    m_message = "棋谱已导出";
    m_messageTime = SDL_GetTicks();
}

void GUI::importPGN(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        m_message = "导入失败: 无法打开文件";
        m_messageTime = SDL_GetTicks();
        return;
    }

    // 重置
    m_board.reset();
    m_moveHistory.clear();
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_hasLastMove = false;
    m_viewIndex = -1;
    m_moveListScroll = 0;

    std::string startFEN;
    std::string line;
    std::vector<std::string> moves;

    // 解析PGN
    while (std::getline(ifs, line)) {
        // 跳过空行
        if (line.empty()) continue;
        // 头部标签
        if (line[0] == '[') {
            // 提取FEN
            if (line.find("[FEN") != std::string::npos) {
                auto q1 = line.find('"');
                auto q2 = line.rfind('"');
                if (q1 != std::string::npos && q2 > q1) {
                    startFEN = line.substr(q1 + 1, q2 - q1 - 1);
                }
            }
            continue;
        }
        // 走法行：解析UCI走法 (跳过回合号、注释、结果)
        std::istringstream ss(line);
        std::string token;
        while (ss >> token) {
            // 跳过结果标记
            if (token == "*" || token == "1-0" || token == "0-1" || token == "1/2-1/2")
                continue;
            // 跳过回合号 (如 "1." "2.")
            if (!token.empty() && token.back() == '.')
                continue;
            // 跳过注释 { ... }
            if (token[0] == '{') {
                // 读到 } 为止
                if (token.back() != '}') {
                    std::string skip;
                    while (ss >> skip) {
                        if (!skip.empty() && skip.back() == '}') break;
                    }
                }
                continue;
            }
            // UCI 走法格式检查 (4个字符, 如 h2e2)
            if (token.size() >= 4 && token[0] >= 'a' && token[0] <= 'i' &&
                token[1] >= '0' && token[1] <= '9') {
                moves.push_back(token.substr(0, 4));
            }
        }
    }

    // 如果有FEN就先设置
    if (!startFEN.empty()) {
        m_board.fromFEN(startFEN);
    }

    // 回放走法
    for (auto& uci : moves) {
        Move move = Board::uciToMove(uci, m_board);
        if (move.fromRow < 0) break;
        move.captured = m_board.get(move.toRow, move.toCol);

        std::string chinese = uciToChineseNotation(uci, m_board);
        std::string fenBefore = m_board.toFEN();
        m_moveHistory.push_back({move, m_board.currentSide(), uci, chinese, fenBefore});

        m_board.makeMove(move);
        m_board.switchSide();
    }

    if (!m_moveHistory.empty()) {
        m_lastMove = m_moveHistory.back().move;
        m_hasLastMove = true;
    }

    m_message = "已导入 " + std::to_string(m_moveHistory.size()) + " 步棋谱";
    m_messageTime = SDL_GetTicks();
}

void GUI::openPGNExportDialog() {
    // 直接导出到当前目录
    std::string path = "xiangqi_game.pgn";
    exportPGN(path);
}

void GUI::openPGNImportDialog() {
    // 使用文件浏览器（复用现有的，但用于PGN文件选择）
    // 简化：直接从当前目录导入
    std::string path = "xiangqi_game.pgn";
    if (fs::exists(path)) {
        importPGN(path);
    } else {
        m_message = "未找到 xiangqi_game.pgn";
        m_messageTime = SDL_GetTicks();
    }
}

int GUI::moveListHitTest(int x, int y) const {
    auto lay = m_renderer.calcPanelLayout();
    return m_renderer.moveListHitTest(x, y, (int)m_moveHistory.size(),
                                       m_moveListScroll, lay.moveListTop, lay.moveListHeight);
}

void GUI::moveListHandleScroll(int dy) {
    m_moveListScroll -= dy;
    int maxScroll = std::max(0, ((int)m_moveHistory.size() + 1) / 2 - 5);
    m_moveListScroll = std::max(0, std::min(m_moveListScroll, maxScroll));
}

// ========== 计时器 ==========

void GUI::startMoveTimer() {
    m_currentMoveStartTime = SDL_GetTicks();
}

void GUI::stopMoveTimerAndRecord(Side side) {
    if (m_currentMoveStartTime == 0) return;
    Uint32 elapsed = SDL_GetTicks() - m_currentMoveStartTime;
    m_currentMoveStartTime = 0;
    if (side == Side::RED) {
        m_redTimer.totalMs   += elapsed;
        m_redTimer.lastMoveMs = elapsed;
    } else {
        m_blackTimer.totalMs   += elapsed;
        m_blackTimer.lastMoveMs = elapsed;
    }
}

// ========== 棋局编辑 ==========

void GUI::enterEditMode() {
    if (m_engine.isThinking()) m_engine.sendStop();
    m_uiState = UIState::EDITING;
    m_editPiece = {};              // 默认选橡皮擦
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_currentMoveStartTime = 0;   // 暂停计时
    m_message = "棋局编辑模式: 左键放置, 右键删除";
    m_messageTime = SDL_GetTicks();
}

void GUI::exitEditMode() {
    // ---- 局面合法性检查 ----

    // 仕/士合法位置
    auto validAdvisor = [](int r, int c, Side s) -> bool {
        if (s == Side::RED) {
            return (r==0&&c==3)||(r==0&&c==5)||(r==1&&c==4)||(r==2&&c==3)||(r==2&&c==5);
        } else {
            return (r==7&&c==3)||(r==7&&c==5)||(r==8&&c==4)||(r==9&&c==3)||(r==9&&c==5);
        }
    };
    // 相/象合法位置
    auto validBishop = [](int r, int c, Side s) -> bool {
        if (s == Side::RED) {
            return (r==0&&c==2)||(r==0&&c==6)||(r==2&&c==0)||(r==2&&c==4)||(r==2&&c==8)
                 ||(r==4&&c==2)||(r==4&&c==6);
        } else {
            return (r==5&&c==2)||(r==5&&c==6)||(r==7&&c==0)||(r==7&&c==4)||(r==7&&c==8)
                 ||(r==9&&c==2)||(r==9&&c==6);
        }
    };
    // 兵/卒合法范围：红兵 row>=3，黑卒 row<=6
    auto validPawn = [](int r, Side s) -> bool {
        return s == Side::RED ? r >= 3 : r <= 6;
    };
    // 九宫格
    auto validPalace = [](int r, int c, Side s) -> bool {
        if (c < 3 || c > 5) return false;
        return s == Side::RED ? (r >= 0 && r <= 2) : (r >= 7 && r <= 9);
    };

    // 各类棋子数量上限 (KING=1, ADVISOR=2, BISHOP=2, KNIGHT=2, ROOK=2, CANNON=2, PAWN=5)
    auto maxCount = [](PieceType t) -> int {
        switch (t) {
            case PieceType::KING: return 1;
            case PieceType::ADVISOR: case PieceType::BISHOP:
            case PieceType::KNIGHT:  case PieceType::ROOK:
            case PieceType::CANNON:  return 2;
            case PieceType::PAWN:    return 5;
            default: return 0;
        }
    };
    auto pieceName = [](PieceType t, Side s) -> std::string {
        if (s == Side::RED) {
            switch (t) {
                case PieceType::KING: return "帅";  case PieceType::ADVISOR: return "仕";
                case PieceType::BISHOP: return "相"; case PieceType::KNIGHT: return "马";
                case PieceType::ROOK: return "车";   case PieceType::CANNON: return "炮";
                case PieceType::PAWN: return "兵";   default: return "?";
            }
        } else {
            switch (t) {
                case PieceType::KING: return "将";  case PieceType::ADVISOR: return "士";
                case PieceType::BISHOP: return "象"; case PieceType::KNIGHT: return "马";
                case PieceType::ROOK: return "车";   case PieceType::CANNON: return "炮";
                case PieceType::PAWN: return "卒";   default: return "?";
            }
        }
    };

    // 统计棋子数量
    int count[2][8] = {}; // [side-1][type]
    for (int r = 0; r < Board::ROWS; r++) {
        for (int c = 0; c < Board::COLS; c++) {
            Piece p = m_board.get(r, c);
            if (p.empty()) continue;
            int si = (int)p.side - 1; // 0=RED, 1=BLACK
            count[si][(int)p.type]++;

            // 位置合法性
            std::string err;
            switch (p.type) {
                case PieceType::KING:
                    if (!validPalace(r, c, p.side))
                        err = pieceName(p.type, p.side) + "必须在九宫格内";
                    break;
                case PieceType::ADVISOR:
                    if (!validAdvisor(r, c, p.side))
                        err = pieceName(p.type, p.side) + "位置不合法(须在九宫对角线)";
                    break;
                case PieceType::BISHOP:
                    if (!validBishop(r, c, p.side))
                        err = pieceName(p.type, p.side) + "位置不合法(须在己方田字格)";
                    break;
                case PieceType::PAWN:
                    if (!validPawn(r, p.side))
                        err = pieceName(p.type, p.side) + "不能在己方底线区域";
                    break;
                default: break;
            }
            if (!err.empty()) {
                m_message = "局面非法: " + err;
                m_messageTime = SDL_GetTicks();
                return;
            }
        }
    }

    // 检查棋子数量
    for (int si = 0; si < 2; si++) {
        Side s = si == 0 ? Side::RED : Side::BLACK;
        for (int t = (int)PieceType::KING; t <= (int)PieceType::PAWN; t++) {
            PieceType pt = (PieceType)t;
            if (count[si][t] > maxCount(pt)) {
                m_message = "局面非法: " + std::string(s==Side::RED?"红":"黑") + "方"
                    + pieceName(pt, s) + "数量超限(最多" + std::to_string(maxCount(pt)) + "个)";
                m_messageTime = SDL_GetTicks();
                return;
            }
        }
    }

    // 双方各有且仅有一个将/帅
    if (count[0][(int)PieceType::KING] != 1 || count[1][(int)PieceType::KING] != 1) {
        m_message = "局面非法: 双方必须各有且仅有一个将/帅!";
        m_messageTime = SDL_GetTicks();
        return;
    }
    if (m_board.kingsOpposing()) {
        m_message = "局面非法: 双将对脸 (同一列中间无棋子)!";
        m_messageTime = SDL_GetTicks();
        return;
    }

    // 设置先行方
    m_board.setCurrentSide(m_editFirstSide);

    m_uiState = UIState::NORMAL;
    m_editPiece = {};
    m_moveHistory.clear();
    m_hasLastMove = false;
    m_viewIndex = -1;
    m_noCaptureCount = 0;
    m_redTimer = {};
    m_blackTimer = {};
    startMoveTimer();
    m_message = "编辑完成，" + std::string(m_editFirstSide == Side::RED ? "红" : "黑") + "方先行";
    m_messageTime = SDL_GetTicks();
    // 通知引擎新局
    if (m_engine.isRunning()) {
        m_engine.sendNewGame();
        m_engine.sendIsReady();
    }
    tryStartEngineMove();
}

void GUI::handleEditBoardClick(int row, int col, bool erase) {
    if (row < 0 || row >= Board::ROWS || col < 0 || col >= Board::COLS) return;
    if (erase || m_editPiece.empty()) {
        m_board.set(row, col, Piece{});
    } else {
        m_board.set(row, col, m_editPiece);
    }
}

int GUI::editPaletteHitTest(int x, int y) const {
    return m_renderer.editPaletteHitTest(x, y);
}

void GUI::run() {
    m_running = true;
    SDL_Event ev;
    bool needRedraw = true;

    while (m_running) {
        bool hasEvent = false;
        // 引擎思考中或游戏进行中（需要实时更新计时器），频繁刷新
        bool gameActive = (m_uiState == UIState::NORMAL || m_uiState == UIState::EDITING)
                          && m_currentMoveStartTime > 0;
        if (m_engine.isThinking() || gameActive) {
            while (SDL_PollEvent(&ev)) {
                handleEvent(ev);
                hasEvent = true;
            }
            needRedraw = true;
            SDL_Delay(50); // 思考中 ~20fps 足够显示信息更新
        } else {
            // 空闲时阻塞等待事件，最长 200ms 唤醒一次（处理消息超时等）
            if (SDL_WaitEventTimeout(&ev, 200)) {
                handleEvent(ev);
                hasEvent = true;
                // 处理同一帧内积压的其余事件
                while (SDL_PollEvent(&ev))
                    handleEvent(ev);
                needRedraw = true;
            }
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
