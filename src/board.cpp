#include "board.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

Board::Board() {
    reset();
}

void Board::reset() {
    // 清空棋盘
    for (auto& row : m_board)
        for (auto& p : row)
            p = Piece{};

    // 红方 (row 0-4, 底部)
    auto R = Side::RED;
    set(0, 0, {PieceType::ROOK, R});    set(0, 8, {PieceType::ROOK, R});
    set(0, 1, {PieceType::KNIGHT, R});  set(0, 7, {PieceType::KNIGHT, R});
    set(0, 2, {PieceType::BISHOP, R});  set(0, 6, {PieceType::BISHOP, R});
    set(0, 3, {PieceType::ADVISOR, R}); set(0, 5, {PieceType::ADVISOR, R});
    set(0, 4, {PieceType::KING, R});
    set(2, 1, {PieceType::CANNON, R});  set(2, 7, {PieceType::CANNON, R});
    set(3, 0, {PieceType::PAWN, R});    set(3, 2, {PieceType::PAWN, R});
    set(3, 4, {PieceType::PAWN, R});    set(3, 6, {PieceType::PAWN, R});
    set(3, 8, {PieceType::PAWN, R});

    // 黑方 (row 5-9, 顶部)
    auto B = Side::BLACK;
    set(9, 0, {PieceType::ROOK, B});    set(9, 8, {PieceType::ROOK, B});
    set(9, 1, {PieceType::KNIGHT, B});  set(9, 7, {PieceType::KNIGHT, B});
    set(9, 2, {PieceType::BISHOP, B});  set(9, 6, {PieceType::BISHOP, B});
    set(9, 3, {PieceType::ADVISOR, B}); set(9, 5, {PieceType::ADVISOR, B});
    set(9, 4, {PieceType::KING, B});
    set(7, 1, {PieceType::CANNON, B});  set(7, 7, {PieceType::CANNON, B});
    set(6, 0, {PieceType::PAWN, B});    set(6, 2, {PieceType::PAWN, B});
    set(6, 4, {PieceType::PAWN, B});    set(6, 6, {PieceType::PAWN, B});
    set(6, 8, {PieceType::PAWN, B});

    m_current = Side::RED;
    m_history.clear();
}

Piece Board::get(int row, int col) const {
    if (!inBoard(row, col)) return Piece{};
    return m_board[row][col];
}

void Board::set(int row, int col, Piece p) {
    if (inBoard(row, col))
        m_board[row][col] = p;
}

void Board::switchSide() {
    m_current = (m_current == Side::RED) ? Side::BLACK : Side::RED;
}

bool Board::inPalace(int r, int c, Side s) const {
    if (c < 3 || c > 5) return false;
    if (s == Side::RED) return r >= 0 && r <= 2;
    return r >= 7 && r <= 9;
}

std::pair<int,int> Board::findKing(Side s) const {
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (m_board[r][c].type == PieceType::KING && m_board[r][c].side == s)
                return {r, c};
    return {-1, -1};
}

bool Board::kingsOpposing() const {
    auto [rr, rc] = findKing(Side::RED);
    auto [br, bc] = findKing(Side::BLACK);
    if (rc != bc) return false;
    // 检查两王之间是否有棋子
    int minR = std::min(rr, br), maxR = std::max(rr, br);
    for (int r = minR + 1; r < maxR; r++)
        if (!m_board[r][rc].empty()) return false;
    return true;
}

// ========== 走法生成 ==========
std::vector<Move> Board::genKingMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& d : dirs) {
        int nr = r + d[0], nc = c + d[1];
        if (!inPalace(nr, nc, s)) continue;
        auto& t = m_board[nr][nc];
        if (t.empty() || t.side != s)
            moves.push_back({r, c, nr, nc, t});
    }
    return moves;
}

std::vector<Move> Board::genAdvisorMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int dirs[][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (auto& d : dirs) {
        int nr = r + d[0], nc = c + d[1];
        if (!inPalace(nr, nc, s)) continue;
        auto& t = m_board[nr][nc];
        if (t.empty() || t.side != s)
            moves.push_back({r, c, nr, nc, t});
    }
    return moves;
}

std::vector<Move> Board::genBishopMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int dirs[][2] = {{-2,-2},{-2,2},{2,-2},{2,2}};
    int eyes[][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    for (int i = 0; i < 4; i++) {
        int nr = r + dirs[i][0], nc = c + dirs[i][1];
        int er = r + eyes[i][0], ec = c + eyes[i][1];
        if (!inBoard(nr, nc)) continue;
        // 象不能过河
        if (s == Side::RED && nr > 4) continue;
        if (s == Side::BLACK && nr < 5) continue;
        // 塞象眼
        if (!m_board[er][ec].empty()) continue;
        auto& t = m_board[nr][nc];
        if (t.empty() || t.side != s)
            moves.push_back({r, c, nr, nc, t});
    }
    return moves;
}

std::vector<Move> Board::genKnightMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int jumps[][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    int legs[][2]  = {{-1,0},{-1,0},{0,-1},{0,1},{0,-1},{0,1},{1,0},{1,0}};
    for (int i = 0; i < 8; i++) {
        int nr = r + jumps[i][0], nc = c + jumps[i][1];
        int lr = r + legs[i][0], lc = c + legs[i][1];
        if (!inBoard(nr, nc)) continue;
        // 蹩马腿
        if (!m_board[lr][lc].empty()) continue;
        auto& t = m_board[nr][nc];
        if (t.empty() || t.side != s)
            moves.push_back({r, c, nr, nc, t});
    }
    return moves;
}

std::vector<Move> Board::genRookMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& d : dirs) {
        for (int i = 1; i < 10; i++) {
            int nr = r + d[0]*i, nc = c + d[1]*i;
            if (!inBoard(nr, nc)) break;
            auto& t = m_board[nr][nc];
            if (t.empty()) {
                moves.push_back({r, c, nr, nc, t});
            } else {
                if (t.side != s)
                    moves.push_back({r, c, nr, nc, t});
                break;
            }
        }
    }
    return moves;
}

std::vector<Move> Board::genCannonMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    int dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (auto& d : dirs) {
        bool jumped = false;
        for (int i = 1; i < 10; i++) {
            int nr = r + d[0]*i, nc = c + d[1]*i;
            if (!inBoard(nr, nc)) break;
            auto& t = m_board[nr][nc];
            if (!jumped) {
                if (t.empty()) {
                    moves.push_back({r, c, nr, nc, t});
                } else {
                    jumped = true;
                }
            } else {
                if (!t.empty()) {
                    if (t.side != s)
                        moves.push_back({r, c, nr, nc, t});
                    break;
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::genPawnMoves(int r, int c, Side s) const {
    std::vector<Move> moves;
    auto tryAdd = [&](int nr, int nc) {
        if (!inBoard(nr, nc)) return;
        auto& t = m_board[nr][nc];
        if (t.empty() || t.side != s)
            moves.push_back({r, c, nr, nc, t});
    };

    if (s == Side::RED) {
        tryAdd(r + 1, c); // 前进
        if (r >= 5) { // 过河
            tryAdd(r, c - 1);
            tryAdd(r, c + 1);
        }
    } else {
        tryAdd(r - 1, c);
        if (r <= 4) {
            tryAdd(r, c - 1);
            tryAdd(r, c + 1);
        }
    }
    return moves;
}

std::vector<Move> Board::getLegalMoves(int row, int col) const {
    Piece p = get(row, col);
    if (p.empty()) return {};

    std::vector<Move> raw;
    switch (p.type) {
        case PieceType::KING:    raw = genKingMoves(row, col, p.side); break;
        case PieceType::ADVISOR: raw = genAdvisorMoves(row, col, p.side); break;
        case PieceType::BISHOP:  raw = genBishopMoves(row, col, p.side); break;
        case PieceType::KNIGHT:  raw = genKnightMoves(row, col, p.side); break;
        case PieceType::ROOK:    raw = genRookMoves(row, col, p.side); break;
        case PieceType::CANNON:  raw = genCannonMoves(row, col, p.side); break;
        case PieceType::PAWN:    raw = genPawnMoves(row, col, p.side); break;
        default: break;
    }

    // 过滤：走棋后不能被将军，且不能将帅照面
    std::vector<Move> legal;
    Board* self = const_cast<Board*>(this);
    for (auto& m : raw) {
        self->makeMove(m);
        bool inCheck = self->isInCheck(p.side);
        bool opposing = self->kingsOpposing();
        self->undoMove(m);
        if (!inCheck && !opposing)
            legal.push_back(m);
    }
    return legal;
}

bool Board::isLegalMove(int fr, int fc, int tr, int tc) const {
    auto moves = getLegalMoves(fr, fc);
    for (auto& m : moves)
        if (m.toRow == tr && m.toCol == tc) return true;
    return false;
}

void Board::makeMove(const Move& move) {
    m_board[move.toRow][move.toCol] = m_board[move.fromRow][move.fromCol];
    m_board[move.fromRow][move.fromCol] = Piece{};
}

void Board::undoMove(const Move& move) {
    m_board[move.fromRow][move.fromCol] = m_board[move.toRow][move.toCol];
    m_board[move.toRow][move.toCol] = move.captured;
}

bool Board::isInCheck(Side side) const {
    auto [kr, kc] = findKing(side);
    if (kr < 0) return true; // 王被吃了
    Side opp = (side == Side::RED) ? Side::BLACK : Side::RED;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            auto& p = m_board[r][c];
            if (p.empty() || p.side != opp) continue;
            std::vector<Move> moves;
            switch (p.type) {
                case PieceType::KING:    moves = genKingMoves(r, c, opp); break;
                case PieceType::ADVISOR: moves = genAdvisorMoves(r, c, opp); break;
                case PieceType::BISHOP:  moves = genBishopMoves(r, c, opp); break;
                case PieceType::KNIGHT:  moves = genKnightMoves(r, c, opp); break;
                case PieceType::ROOK:    moves = genRookMoves(r, c, opp); break;
                case PieceType::CANNON:  moves = genCannonMoves(r, c, opp); break;
                case PieceType::PAWN:    moves = genPawnMoves(r, c, opp); break;
                default: break;
            }
            for (auto& m : moves)
                if (m.toRow == kr && m.toCol == kc) return true;
        }
    }
    return false;
}

// FEN 转换
static char pieceToFENChar(Piece p) {
    if (p.empty()) return ' ';
    char c = ' ';
    switch (p.type) {
        case PieceType::KING:    c = 'k'; break;
        case PieceType::ADVISOR: c = 'a'; break;
        case PieceType::BISHOP:  c = 'b'; break;
        case PieceType::KNIGHT:  c = 'n'; break;
        case PieceType::ROOK:    c = 'r'; break;
        case PieceType::CANNON:  c = 'c'; break;
        case PieceType::PAWN:    c = 'p'; break;
        default: break;
    }
    if (p.side == Side::RED) c = std::toupper(c);
    return c;
}

static Piece fenCharToPiece(char c) {
    Side s = std::isupper(c) ? Side::RED : Side::BLACK;
    PieceType t = PieceType::NONE;
    switch (std::tolower(c)) {
        case 'k': t = PieceType::KING; break;
        case 'a': t = PieceType::ADVISOR; break;
        case 'b': case 'e': t = PieceType::BISHOP; break;
        case 'n': case 'h': t = PieceType::KNIGHT; break;
        case 'r': t = PieceType::ROOK; break;
        case 'c': t = PieceType::CANNON; break;
        case 'p': t = PieceType::PAWN; break;
    }
    return {t, s};
}

std::string Board::toFEN() const {
    std::string fen;
    for (int r = ROWS - 1; r >= 0; r--) {
        int empty = 0;
        for (int c = 0; c < COLS; c++) {
            if (m_board[r][c].empty()) {
                empty++;
            } else {
                if (empty > 0) { fen += std::to_string(empty); empty = 0; }
                fen += pieceToFENChar(m_board[r][c]);
            }
        }
        if (empty > 0) fen += std::to_string(empty);
        if (r > 0) fen += '/';
    }
    fen += (m_current == Side::RED) ? " w" : " b";
    fen += " - - 0 1";
    return fen;
}

void Board::fromFEN(const std::string& fen) {
    for (auto& row : m_board)
        for (auto& p : row)
            p = Piece{};
    m_history.clear();

    std::istringstream ss(fen);
    std::string boardPart;
    ss >> boardPart;

    int r = ROWS - 1, c = 0;
    for (char ch : boardPart) {
        if (ch == '/') {
            r--;
            c = 0;
            if (r < 0) break;
        } else if (std::isdigit(ch)) {
            c += ch - '0';
        } else {
            if (inBoard(r, c))
                m_board[r][c] = fenCharToPiece(ch);
            c++;
        }
    }

    std::string sideStr;
    if (ss >> sideStr)
        m_current = (sideStr == "w" || sideStr == "r") ? Side::RED : Side::BLACK;
    else
        m_current = Side::RED;
}

std::string Board::moveToUCI(const Move& m) {
    std::string s;
    s += (char)('a' + m.fromCol);
    s += std::to_string(m.fromRow);
    s += (char)('a' + m.toCol);
    s += std::to_string(m.toRow);
    return s;
}

Move Board::uciToMove(const std::string& uci, const Board& board) {
    if (uci.size() < 4) return {-1,-1,-1,-1, {}};
    int fc = uci[0] - 'a';
    int fr = uci[1] - '0';
    int tc = uci[2] - 'a';
    int tr = uci[3] - '0';
    if (fc < 0 || fc >= COLS || fr < 0 || fr >= ROWS ||
        tc < 0 || tc >= COLS || tr < 0 || tr >= ROWS)
        return {-1,-1,-1,-1, {}};
    Piece cap = board.get(tr, tc);
    return {fr, fc, tr, tc, cap};
}
