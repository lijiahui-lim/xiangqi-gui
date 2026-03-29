#include "rules.h"
#include "board.h"

// ========== 一将一捉无根子检测 ==========

// 判断 (r,c) 处的棋子是否有根（被友方棋子保护）
// 方法: 将该位置替换为敌方棋子，看友方能否吃到该格
static bool isPieceProtected(Board& board, int r, int c) {
    Piece target = board.get(r, c);
    if (target.empty()) return false;
    Side friendly = target.side;
    Side enemy = (friendly == Side::RED) ? Side::BLACK : Side::RED;

    // 临时替换为敌方兵/卒，这样友方的"吃子走法"就能覆盖此格
    Piece saved = target;
    board.set(r, c, {PieceType::PAWN, enemy});

    bool defended = false;
    for (int fr = 0; fr < Board::ROWS && !defended; fr++) {
        for (int fc = 0; fc < Board::COLS && !defended; fc++) {
            if (fr == r && fc == c) continue;
            Piece p = board.get(fr, fc);
            if (p.side != friendly) continue;
            auto moves = board.getLegalMoves(fr, fc);
            for (const auto& m : moves) {
                if (m.toRow == r && m.toCol == c) {
                    defended = true;
                    break;
                }
            }
        }
    }

    board.set(r, c, saved);
    return defended;
}

// 检测局面中进攻方（刚走完棋的一方）是否在"捉"对方的无根子
// board 的 currentSide 是受害方（即将走棋方）
static bool hasThreatenedUnprotectedPiece(Board& board) {
    Side victim = board.currentSide();
    Side attacker = (victim == Side::RED) ? Side::BLACK : Side::RED;

    for (int fr = 0; fr < Board::ROWS; fr++) {
        for (int fc = 0; fc < Board::COLS; fc++) {
            Piece p = board.get(fr, fc);
            if (p.side != attacker) continue;
            auto moves = board.getLegalMoves(fr, fc);
            for (const auto& m : moves) {
                Piece target = board.get(m.toRow, m.toCol);
                if (!target.empty() && target.side == victim &&
                    target.type != PieceType::KING) {
                    if (!isPieceProtected(board, m.toRow, m.toCol)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

int RuleEngine::detectCheckCaptureCycle(const std::vector<std::string>& posHistory) const {
    int n = (int)posHistory.size();
    if (n < 4) return 0;  // 至少需要 4 个半步才可能形成一个循环

    // 分析进攻方的最近走法:
    // posHistory[n-1], posHistory[n-3], posHistory[n-5] 是同一方（受害方）的局面
    // 在这些局面中，进攻方刚走完棋
    // 检查是否呈现: 将 + 捉 交替的模式

    int checkCount = 0;
    int captureCount = 0;
    int analyzedCount = 0;

    for (int k = 1; k <= 6 && (n - k) >= 0; k += 2) {
        int idx = n - k;
        Board b;
        b.fromFEN(posHistory[idx]);
        Side victim = b.currentSide();

        bool inCheck = b.isInCheck(victim);
        bool hasThreat = false;
        if (!inCheck) {
            hasThreat = hasThreatenedUnprotectedPiece(b);
        }

        if (inCheck) {
            checkCount++;
        } else if (hasThreat) {
            captureCount++;
        } else {
            break;  // 模式中断
        }
        analyzedCount++;
    }

    // 一将一捉: 需要同时存在"将"和"捉无根子"
    if (checkCount >= 1 && captureCount >= 1 && analyzedCount >= 2) {
        return analyzedCount;  // 返回循环中的进攻步数
    }

    return 0;
}
