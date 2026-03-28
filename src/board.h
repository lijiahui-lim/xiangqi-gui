#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>

// 棋子类型
enum class PieceType : uint8_t {
    NONE = 0,
    KING,    // 帅/将
    ADVISOR, // 仕/士
    BISHOP,  // 相/象
    KNIGHT,  // 马
    ROOK,    // 车
    CANNON,  // 炮
    PAWN     // 兵/卒
};

// 阵营
enum class Side : uint8_t {
    NONE = 0,
    RED,
    BLACK
};

struct Piece {
    PieceType type = PieceType::NONE;
    Side side = Side::NONE;

    bool empty() const { return type == PieceType::NONE; }
};

struct Move {
    int fromRow, fromCol;
    int toRow, toCol;
    Piece captured; // 被吃的棋子

    bool operator==(const Move& o) const {
        return fromRow == o.fromRow && fromCol == o.fromCol &&
               toRow == o.toRow && toCol == o.toCol;
    }
};

// 棋盘 10行9列, row 0 = 红方底部, row 9 = 黑方底部
class Board {
public:
    static constexpr int ROWS = 10;
    static constexpr int COLS = 9;

    Board();

    void reset(); // 初始布局
    Piece get(int row, int col) const;
    void set(int row, int col, Piece p);

    Side currentSide() const { return m_current; }
    void setCurrentSide(Side s) { m_current = s; }
    void switchSide();

    // 走法合法性检查
    bool isLegalMove(int fr, int fc, int tr, int tc) const;
    // 获取某棋子的所有合法走法
    std::vector<Move> getLegalMoves(int row, int col) const;
    // 执行走法
    void makeMove(const Move& move);
    // 撤销走法
    void undoMove(const Move& move);

    // 是否被将军
    bool isInCheck(Side side) const;
    // 是否将帅照面
    bool kingsOpposing() const;

    // FEN 字符串
    std::string toFEN() const;
    void fromFEN(const std::string& fen);

    // UCI走法格式 (例如 "h2e2")
    static std::string moveToUCI(const Move& m);
    static Move uciToMove(const std::string& uci, const Board& board);

    // 走法历史
    const std::vector<Move>& history() const { return m_history; }

private:
    std::array<std::array<Piece, COLS>, ROWS> m_board;
    Side m_current = Side::RED;
    std::vector<Move> m_history;

    bool inBoard(int r, int c) const { return r >= 0 && r < ROWS && c >= 0 && c < COLS; }
    bool inPalace(int r, int c, Side s) const;

    // 各棋子走法生成（不含将军检测）
    std::vector<Move> genKingMoves(int r, int c, Side s) const;
    std::vector<Move> genAdvisorMoves(int r, int c, Side s) const;
    std::vector<Move> genBishopMoves(int r, int c, Side s) const;
    std::vector<Move> genKnightMoves(int r, int c, Side s) const;
    std::vector<Move> genRookMoves(int r, int c, Side s) const;
    std::vector<Move> genCannonMoves(int r, int c, Side s) const;
    std::vector<Move> genPawnMoves(int r, int c, Side s) const;

    // 找到将/帅位置
    std::pair<int,int> findKing(Side s) const;
};
