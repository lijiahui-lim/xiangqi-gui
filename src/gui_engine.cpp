#include "gui.h"
#include <sstream>
#include <cstring>

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
        // 通过SDL事件传递到主线程，避免跨线程访问棋盘数据
        SDL_Event ev;
        SDL_memset(&ev, 0, sizeof(ev));
        ev.type = SDL_USEREVENT;
        ev.user.code = 2;
        ev.user.data1 = new EngineInfo(info);
        if (SDL_PushEvent(&ev) < 1) {
            delete (EngineInfo*)ev.user.data1;
        }
    });
    m_engine.setBestMoveCallback([this](const std::string& uci) {
        // 通过SDL事件传递到主线程
        SDL_Event ev;
        SDL_memset(&ev, 0, sizeof(ev));
        ev.type = SDL_USEREVENT;
        ev.user.code = 1;
        // 存bestmove字符串
        char* data = new char[uci.size() + 1];
        strcpy(data, uci.c_str());
        ev.user.data1 = data;
        if (SDL_PushEvent(&ev) < 1) {
            delete[] data;  // 推送失败时释放内存
        }
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

    // 如果有被禁止的走法（引擎违例后变招），用 searchmoves 限制搜索范围
    std::vector<std::string> searchmoves;
    if (!m_engineBannedMoves.empty()) {
        // 生成当前方所有合法走法
        for (int r = 0; r < Board::ROWS; r++) {
            for (int c = 0; c < Board::COLS; c++) {
                auto piece = m_board.get(r, c);
                if (piece.side == m_board.currentSide()) {
                    auto moves = m_board.getLegalMoves(r, c);
                    for (auto& m : moves) {
                        std::string u = Board::moveToUCI(m);
                        // 排除被禁止的走法
                        bool banned = false;
                        for (const auto& b : m_engineBannedMoves) {
                            if (u == b) { banned = true; break; }
                        }
                        if (!banned) searchmoves.push_back(u);
                    }
                }
            }
        }
        // 如果所有走法都被禁了，无法变招，清除禁止列表正常搜索
        if (searchmoves.empty()) {
            m_engineBannedMoves.clear();
            searchmoves.clear();
        }
    }

    if (m_settings.searchTime > 0)
        m_engine.sendGo(0, m_settings.searchTime, 0, 0, searchmoves);
    else
        m_engine.sendGo(m_settings.searchDepth, 0, 0, 0, searchmoves);
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
    // 如果引擎正在思考，先停止旧搜索（用 sendStopForRestart 保证旧 bestmove 不会破坏状态）
    if (m_engine.isThinking())
        m_engine.sendStopForRestart();

    // 构造走法列表（仅包含当前浏览位置前的走法）
    std::vector<std::string> uciMoves;
    size_t moveLimit = (m_viewIndex >= 0) ? m_viewIndex + 1 : m_moveHistory.size();
    for (size_t i = 0; i < moveLimit && i < m_moveHistory.size(); i++)
        uciMoves.push_back(Board::moveToUCI(m_moveHistory[i].move));

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
        // 进入分析模式
        if (!m_engine.isRunning()) {
            m_message = "引擎未运行，无法分析";
            m_messageTime = SDL_GetTicks();
            m_analysisMode = false;
            return;
        }
        // 不要在这里手动 sendStop —— startAnalysis() 内部使用 sendStopForRestart()
        // 可以正确保护 m_thinking 标志不被旧 bestmove 破坏
        m_message = "分析模式: 开";
        m_messageTime = SDL_GetTicks();
        initButtons();
        // 启动分析（支持浏览历史时分析）
        if (m_uiState != UIState::GAME_OVER)
            startAnalysis();
    } else {
        // 退出分析模式
        m_engineInfoText.clear();
        m_pvArrow1.valid = false;
        m_pvArrow2.valid = false;
        m_message = "分析模式: 关";
        m_messageTime = SDL_GetTicks();
        initButtons();

        // 判断是否需要立即启动新搜索
        bool willRestart = !isViewingHistory() && m_uiState != UIState::GAME_OVER
                           && isEngineTurn() && m_engine.isRunning();
        if (m_engine.isThinking()) {
            if (willRestart) {
                // 将立即启动新搜索，用 sendStopForRestart 防止旧 bestmove 破坏 m_thinking
                m_engine.sendStopForRestart();
            } else {
                // 仅停止，不重启
                m_ignoreNextBestMove = true;
                m_engine.sendStop();
            }
        }
        // 非浏览历史时，如果轮到引擎走，恢复引擎走棋
        if (willRestart)
            startEngineMove();
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
    m_positionHistory.push_back(m_board.toFEN());

    // === 引擎违例检测：长将 / 一将一捉无根子 ===
    {
        std::string curFen = m_board.toFEN();
        int repeatCount = 0;
        for (const auto& f : m_positionHistory) {
            if (f == curFen) repeatCount++;
        }
        bool opponentInCheck = m_board.isInCheck(m_board.currentSide());

        bool shouldRetract = false;
        std::string retractReason;

        // 1. 长将违例：局面重复 >= 2 次且对方被将军
        if (repeatCount >= 2 && opponentInCheck) {
            shouldRetract = true;
            retractReason = "引擎长将违例, 自动变招";
        }

        // 2. 一将一捉无根子违例
        if (!shouldRetract) {
            int ccCycle = m_rules.detectCheckCaptureCycle(m_positionHistory);
            if (ccCycle >= 2) {
                shouldRetract = true;
                retractReason = "引擎一将一捉违例, 自动变招";
            }
        }

        if (shouldRetract) {
            // 撤销引擎的走法（此时 noCaptureCount 尚未更新，无需恢复）
            m_positionHistory.pop_back();
            m_board.undoMove(move);
            m_board.switchSide();
            m_moveHistory.pop_back();

            // 把这个走法加入禁止列表
            m_engineBannedMoves.push_back(uciStr);

            // 重新让引擎搜索（排除违例走法）
            m_message = retractReason;
            m_messageTime = SDL_GetTicks();
            startEngineMove();
            return;
        }
    }
    // === 违例检测结束 ===

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

    // 引擎走了合法的着，清除禁止列表
    m_engineBannedMoves.clear();

    // 检查规则
    if (!checkGameRules()) return;

    // 查询云库
    if (m_settings.useCloudBook && m_cloudBook.isEnabled()) {
        m_cloudBook.query(m_board.toFEN(), [](const std::vector<CloudBookMove>& moves, const std::string& error) {
            SDL_Event ev; SDL_memset(&ev, 0, sizeof(ev));
            ev.type = SDL_USEREVENT; ev.user.code = 3;
            ev.user.data1 = new CloudBookEventData{moves, error};
            if (SDL_PushEvent(&ev) < 1) delete (CloudBookEventData*)ev.user.data1;
        });
    }

    // 若下一方也由引擎控制，继续走
    // 走棋后清除旧箭头
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    tryStartEngineMove();
}

void GUI::onEngineInfo(const EngineInfo& info) {
    // 等待丢弃 bestmove 期间忽略残留的 info，防止箭头重新出现
    if (m_ignoreNextBestMove) return;

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
        const Board& cur = displayBoard();
        tmpBoard.fromFEN(cur.toFEN());
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
    // 在临时棋盘上模拟走法，逐步转换（使用当前显示的棋盘，支持棋谱浏览）
    Board tmpBoard;
    const Board& cur = displayBoard();
    tmpBoard.fromFEN(cur.toFEN());

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
    if (!error.empty()) {
        m_cloudErrorText = error;
        m_cloudMoves.clear();
        return;
    }
    m_cloudMoves = moves;
    m_cloudErrorText.clear();
    // 把UCI走法转换为中文记法存入note字段（主线程安全访问 m_board）
    for (auto& cm : m_cloudMoves) {
        cm.note = uciToChineseNotation(cm.move, m_board);
    }
}
