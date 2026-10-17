#include "gui.h"
#include <sstream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <ctime>

namespace fs = std::filesystem;

// ========== 棋谱功能 ==========

const Board& GUI::displayBoard() const {
    if (m_viewIndex >= 0) return m_viewBoard;
    return m_board;
}

// m_viewIndex 语义: -1 = 实时局面; >=0 = 已走 viewIndex 步之后的局面
// (0 = 初始局面, N = 第 N 步之后)
void GUI::navigateTo(int index) {
    if (m_moveHistory.empty()) return;

    if (index < 0 || index >= (int)m_moveHistory.size()) {
        // 走出全部历史即回到实时局面
        m_viewIndex = -1;
    } else {
        m_viewIndex = index;

        // 从开局 FEN 重放 index 步（必须用工整记录的 fenBefore，
        // 不能用 reset() —— 编辑局面/导入 FEN 开局的对局会回放出错误盘面）
        m_viewBoard.fromFEN(m_moveHistory.front().fenBefore);
        for (int i = 0; i < index; i++) {
            auto& rec = m_moveHistory[i];
            m_viewBoard.makeMove(rec.move);
            m_viewBoard.switchSide();
        }
    }

    // 分析模式下重新分析新位置
    if (m_analysisMode && m_engine.isRunning()) {
        startAnalysis();
    }
}

void GUI::navigateForward() {
    if (m_moveHistory.empty()) return;
    if (m_viewIndex < 0) return; // 已在实时局面
    navigateTo(m_viewIndex + 1); // 超出末尾自动回到实时
}

void GUI::navigateBackward() {
    if (m_moveHistory.empty()) return;
    if (m_viewIndex < 0) {
        // 从实时回退一步
        navigateTo((int)m_moveHistory.size() - 1);
        return;
    }
    if (m_viewIndex == 0) return; // 已在初始局面
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
    char dateBuf[16] = "";
    {
        std::time_t t = std::time(nullptr);
        std::strftime(dateBuf, sizeof(dateBuf), "%Y.%m.%d", std::localtime(&t));
    }
    ofs << "[Game \"Chinese Chess\"]\n";
    ofs << "[Date \"" << dateBuf << "\"]\n";
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

    // 重置（必须清空全部对局状态，否则残留数据污染规则判定/计时/界面状态）
    // 引擎可能在为旧局面思考：停止并忽略其 bestmove，否则旧结果回来
    // 会被应用到新棋局（恰好合法时就真走错棋了）
    if (m_engine.isThinking()) {
        m_ignoreNextBestMove = true;
        m_engine.sendStop();
    }
    m_board.reset();
    m_moveHistory.clear();
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_hasLastMove = false;
    m_viewIndex = -1;
    m_moveListScroll = 0;
    m_positionHistory.clear();
    m_engineBannedMoves.clear();
    m_noCaptureCount = 0;
    m_cloudMoves.clear();
    m_cloudErrorText.clear();
    m_analysisMode = false;
    m_engineInfoText.clear();
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    m_redTimer = {};
    m_blackTimer = {};
    m_uiState = UIState::NORMAL;

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
    // 起始局面计入重复检测
    m_positionHistory.push_back(m_board.toFEN());

    // 回放走法（必须验证合法性：坐标合法但起点无子/非当前方/非法走法
    // 会静默破坏棋盘并错位后续所有回放）
    for (auto& uci : moves) {
        Move move = Board::uciToMove(uci, m_board);
        if (move.fromRow < 0) break;
        Piece p = m_board.get(move.fromRow, move.fromCol);
        if (p.empty() || p.side != m_board.currentSide() ||
            !m_board.isLegalMove(move.fromRow, move.fromCol, move.toRow, move.toCol))
            break;
        move.captured = m_board.get(move.toRow, move.toCol);

        std::string chinese = uciToChineseNotation(uci, m_board);
        std::string fenBefore = m_board.toFEN();
        m_moveHistory.push_back({move, m_board.currentSide(), uci, chinese, fenBefore});

        m_board.makeMove(move);
        m_board.switchSide();
        m_positionHistory.push_back(m_board.toFEN());
        if (!move.captured.empty()) m_noCaptureCount = 0;
        else m_noCaptureCount++;
    }

    if (!m_moveHistory.empty()) {
        m_lastMove = m_moveHistory.back().move;
        m_hasLastMove = true;
    }

    // 导入即新对局：重置引擎哈希；若轮到引擎走则开始走棋
    if (m_engine.isRunning()) m_engine.sendNewGame();
    tryStartEngineMove();

    startMoveTimer();
    m_message = "已导入 " + std::to_string(m_moveHistory.size()) + " 步棋谱";
    m_messageTime = SDL_GetTicks();
}

void GUI::openPGNExportDialog() {
    // 导出到当前目录，文件名带时间戳避免覆盖历史棋谱
    char buf[40];
    std::time_t t = std::time(nullptr);
    std::strftime(buf, sizeof(buf), "xiangqi_%Y%m%d_%H%M%S.pgn", std::localtime(&t));
    exportPGN(buf);
}

void GUI::openPGNImportDialog() {
    // 导入当前目录中最新的 xiangqi_*.pgn（兼容旧固定文件名）
    std::string best;
    fs::file_time_type bestTime{};
    try {
        for (auto& e : fs::directory_iterator(".")) {
            std::string name = e.path().filename().string();
            if (name.rfind("xiangqi_", 0) == 0 && e.path().extension() == ".pgn") {
                try {
                    auto t = e.last_write_time();
                    if (best.empty() || t > bestTime) {
                        best = name;
                        bestTime = t;
                    }
                } catch (...) { continue; } // 单个文件异常不应中断整个扫描
            }
        }
    } catch (...) {}
    if (best.empty() && fs::exists("xiangqi_game.pgn"))
        best = "xiangqi_game.pgn";

    if (!best.empty()) {
        importPGN(best);
    } else {
        m_message = "未找到棋谱文件 (xiangqi_*.pgn)";
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
    // 停止引擎并清理所有运行状态
    if (m_engine.isThinking()) {
        m_ignoreNextBestMove = true;
        m_engine.sendStop();
    }
    m_analysisMode = false;
    m_pvArrow1.valid = false;
    m_pvArrow2.valid = false;
    m_engineInfoText.clear();
    m_cloudMoves.clear();
    m_cloudErrorText.clear();
    m_uiState = UIState::EDITING;
    m_viewIndex = -1;              // 浏览历史时点编辑：落子改的是 m_board，
                                 // 必须退出浏览态否则编辑完全不可见
    m_editPiece = {};              // 默认选橡皮擦
    m_selRow = m_selCol = -1;
    m_legalMoves.clear();
    m_currentMoveStartTime = 0;   // 暂停计时
    initButtons();
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
    m_positionHistory.clear();
    // 编辑后的起始局面计入重复检测
    m_positionHistory.push_back(m_board.toFEN());
    m_engineBannedMoves.clear();
    m_hasLastMove = false;
    m_viewIndex = -1;
    m_noCaptureCount = 0;
    m_redTimer = {};
    m_blackTimer = {};
    m_ignoreNextBestMove = false;
    m_cloudMoves.clear();
    m_cloudErrorText.clear();
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
