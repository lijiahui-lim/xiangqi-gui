#include "gui.h"
#include <sstream>
#include <cmath>

void GUI::newGame() {
    // 如果正在分析或引擎思考，先停止
    if (m_engine.isThinking()) {
        m_ignoreNextBestMove = true;
        m_engine.sendStop();
    }
    
    m_board.reset();
    m_uiState = UIState::NORMAL;
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
    m_positionHistory.clear();
    m_engineBannedMoves.clear();
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
        m_cloudBook.query(m_board.toFEN(), [](const std::vector<CloudBookMove>& moves, const std::string& error) {
            SDL_Event ev; SDL_memset(&ev, 0, sizeof(ev));
            ev.type = SDL_USEREVENT; ev.user.code = 3;
            ev.user.data1 = new CloudBookEventData{moves, error};
            if (SDL_PushEvent(&ev) < 1) delete (CloudBookEventData*)ev.user.data1;
        });
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
    m_positionHistory.push_back(m_board.toFEN());
    m_lastMove = move;
    m_hasLastMove = true;
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();

    // 更新无吃子计数
    if (!move.captured.empty())
        m_noCaptureCount = 0;
    else
        m_noCaptureCount++;

    // 人类走了棋，清除引擎禁止列表
    m_engineBannedMoves.clear();

    // 启动下一方计时器
    startMoveTimer();

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

    // 计算当前局面重复次数
    std::string curFen = m_board.toFEN();
    int posRepeatCount = 0;
    for (const auto& f : m_positionHistory) {
        if (f == curFen) posRepeatCount++;
    }

    // 计算长将循环次数：当前方被将军且局面重复时，
    // 检查两次重复之间是否每步都在将军（即对方一直在将军我方）
    int perpetualCheckCount = 0;
    if (posRepeatCount >= 2 && inCheck) {
        // 找到上一次出现此局面的位置
        int lastRepeatIdx = -1;
        for (int i = (int)m_positionHistory.size() - 2; i >= 0; i--) {
            if (m_positionHistory[i] == curFen) {
                lastRepeatIdx = i;
                break;
            }
        }
        if (lastRepeatIdx >= 0) {
            // 检查从 lastRepeatIdx+1 到当前（不含）的每步局面中，
            // 被将军方（上一个走棋者/对方视角）是否每步都在被将
            // 由于 positionHistory[i] 是第 i 步走完后的局面，
            // currentSide 交替变化，我们只检查与当前方相同的局面是否都被将
            bool allChecked = true;
            Board tempBoard;
            for (int i = lastRepeatIdx; i < (int)m_positionHistory.size(); i++) {
                tempBoard.fromFEN(m_positionHistory[i]);
                // 这个局面中当前要走的一方和我们当前一样时，检查是否被将
                if (tempBoard.currentSide() == m_board.currentSide()) {
                    if (!tempBoard.isInCheck(tempBoard.currentSide())) {
                        allChecked = false;
                        break;
                    }
                }
            }
            if (allChecked) {
                perpetualCheckCount = posRepeatCount;
            }
        }
    }

    GameState st;
    st.inCheck = inCheck;
    st.hasLegalMoves = hasLegalMove;
    st.noCaptureCount = m_noCaptureCount;
    st.moveCount = (int)m_moveHistory.size();
    st.currentSide = (m_board.currentSide() == Side::RED) ? 0 : 1;
    st.lastCaptured = (!m_moveHistory.empty() && !m_moveHistory.back().move.captured.empty());
    st.perpetualCheckCount = perpetualCheckCount;
    st.positionRepeatCount = posRepeatCount;

    RuleResult rr = m_rules.evaluate(st);

    // 游戏结束时的统一清理
    auto enterGameOver = [&]() {
        if (m_analysisMode) {
            m_analysisMode = false;
            initButtons();
        }
        if (m_engine.isThinking()) {
            m_ignoreNextBestMove = true;
            m_engine.sendStop();
        }
        m_pvArrow1.valid = false;
        m_pvArrow2.valid = false;
        m_engineInfoText.clear();
        m_messageTime = SDL_GetTicks();
        m_uiState = UIState::GAME_OVER;
    };

    switch (rr.action) {
        case RuleAction::CHECKMATE:
            m_message = rr.message.empty()
                ? ((m_board.currentSide() == Side::RED) ? "红方被将死! 黑方胜!" : "黑方被将死! 红方胜!")
                : rr.message;
            enterGameOver();
            return false;
        case RuleAction::DRAW:
            m_message = rr.message.empty() ? "和棋!" : rr.message;
            enterGameOver();
            return false;
        case RuleAction::LOSE:
            // LOSE = 当前方负。对于一将一捉检测，当前side = 违例方
            m_message = rr.message.empty()
                ? ((m_board.currentSide() == Side::RED) ? "红方负!" : "黑方负!")
                : rr.message + ((m_board.currentSide() == Side::RED) ? " 红方负!" : " 黑方负!");
            enterGameOver();
            return false;
        case RuleAction::WIN:
            // 当前方（被违例方）胜
            m_message = rr.message.empty()
                ? ((m_board.currentSide() == Side::RED) ? "红方胜!" : "黑方胜!")
                : rr.message + ((m_board.currentSide() == Side::RED) ? " 红方胜!" : " 黑方胜!");
            enterGameOver();
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
        enterGameOver();
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
        m_ignoreNextBestMove = true;
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
        if (!m_positionHistory.empty()) m_positionHistory.pop_back();
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
        m_cloudBook.query(m_board.toFEN(), [](const std::vector<CloudBookMove>& moves, const std::string& error) {
            SDL_Event ev; SDL_memset(&ev, 0, sizeof(ev));
            ev.type = SDL_USEREVENT; ev.user.code = 3;
            ev.user.data1 = new CloudBookEventData{moves, error};
            if (SDL_PushEvent(&ev) < 1) delete (CloudBookEventData*)ev.user.data1;
        });
    }

    // 分析模式下重新分析
    if (m_analysisMode) {
        startAnalysis();
    }
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
