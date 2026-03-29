#include "renderer.h"
#include <cmath>
#include <cstring>

Renderer::Renderer() = default;
Renderer::~Renderer() { shutdown(); }

bool Renderer::init(int windowW, int windowH) {
    m_windowW = windowW;
    m_windowH = windowH;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
    if (TTF_Init() < 0) return false;

    m_window = SDL_CreateWindow("中国象棋 - 皮卡鱼引擎",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowW, windowH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED);
    if (!m_window) return false;

    m_renderer = SDL_CreateRenderer(m_window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer) return false;

    // 尝试加载字体 - 优先系统中文字体
    const char* fontPaths[] = {
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };

    for (auto& path : fontPaths) {
        m_fontLarge = TTF_OpenFont(path, 28);
        if (m_fontLarge) {
            m_fontMedium = TTF_OpenFont(path, 20);
            m_fontSmall = TTF_OpenFont(path, 15);
            m_fontPath = path;
            break;
        }
    }

    if (!m_fontLarge) {
        // 最后尝试 fc-match
        FILE* fp = popen("fc-match --format=%{file} ':lang=zh'", "r");
        if (fp) {
            char buf[512];
            if (fgets(buf, sizeof(buf), fp)) {
                buf[strcspn(buf, "\n")] = 0;
                m_fontLarge = TTF_OpenFont(buf, 28);
                if (m_fontLarge) {
                    m_fontMedium = TTF_OpenFont(buf, 20);
                    m_fontSmall = TTF_OpenFont(buf, 15);
                    m_fontPath = buf;
                }
            }
            pclose(fp);
        }
    }

    recalcLayout(windowW, windowH);

    return true;
}

void Renderer::recalcLayout(int windowW, int windowH) {
    m_windowW = windowW;
    m_windowH = windowH;

    const int boardPad    = 8;
    const int CLOCK_GAP   = 6;
    const int PANEL_GAP   = 8;
    const int RIGHT_MARGIN = 10;
    const int MIN_PANEL_W  = 180;

    int panelBase0  = std::max(m_windowH / 50, 10);
    int clockFontSz = std::max(panelBase0 + 3, 13);
    m_clockW = std::max(44, clockFontSz * 5 + 16);

    int cellFromH = (windowH - 40) / 10;
    int availW    = windowW - m_clockW - CLOCK_GAP - PANEL_GAP - MIN_PANEL_W - RIGHT_MARGIN;
    int cellFromW = (availW - boardPad * 2) / 9;
    m_cellSize    = std::max(20, std::min(cellFromH, cellFromW));
    m_pieceRadius = m_cellSize * 2 / 5;

    int boardVisualW    = m_cellSize * 9 + boardPad * 2;
    int boardVisualLeft = m_clockW + CLOCK_GAP;

    m_boardLeft = boardVisualLeft + boardPad + m_cellSize / 2;
    m_boardTop  = std::max(15, (windowH - m_cellSize * 9) / 2);

    m_panelLeft = boardVisualLeft + boardVisualW + PANEL_GAP;

    reloadFonts(m_cellSize);
}

void Renderer::reloadFonts(int cellSize) {
    if (m_fontPath.empty()) return;

    if (m_fontLarge) { TTF_CloseFont(m_fontLarge); m_fontLarge = nullptr; }
    if (m_fontMedium) { TTF_CloseFont(m_fontMedium); m_fontMedium = nullptr; }
    if (m_fontSmall) { TTF_CloseFont(m_fontSmall); m_fontSmall = nullptr; }
    if (m_fontPanel) { TTF_CloseFont(m_fontPanel); m_fontPanel = nullptr; }
    if (m_fontClock) { TTF_CloseFont(m_fontClock); m_fontClock = nullptr; }

    int largeSz = std::max(12, cellSize * 7 / 10);
    int panelBase = std::max(m_windowH / 50, 10);
    int medSz = std::max(panelBase + 4, 14);
    int smallSz = std::max(panelBase + 1, 11);
    int panelSz = std::max(panelBase, 9);
    int clockSz = std::max(panelBase + 3, 13);

    m_fontLarge = TTF_OpenFont(m_fontPath.c_str(), largeSz);
    m_fontMedium = TTF_OpenFont(m_fontPath.c_str(), medSz);
    m_fontSmall = TTF_OpenFont(m_fontPath.c_str(), smallSz);
    m_fontPanel = TTF_OpenFont(m_fontPath.c_str(), panelSz);
    m_fontClock = TTF_OpenFont(m_fontPath.c_str(), clockSz);
    if (m_fontClock) TTF_SetFontStyle(m_fontClock, TTF_STYLE_BOLD);
}

Renderer::PanelLayout Renderer::calcPanelLayout() const {
    PanelLayout lay{};
    int panelTop = 15;
    int panelBottom = m_windowH - 10;

    int bh = std::max(24, m_windowH / 23);
    int gap = std::max(4, bh / 5);
    int btnRows = 6;
    lay.buttonsHeight = btnRows * bh + (btnRows - 1) * gap;
    lay.buttonsTop = panelBottom - lay.buttonsHeight;

    int contentH = lay.buttonsTop - panelTop - gap;
    lay.infoTop = panelTop;
    lay.infoHeight = std::max(80, contentH * 45 / 100);
    lay.moveListTop = lay.infoTop + lay.infoHeight + 4;
    lay.moveListHeight = lay.buttonsTop - lay.moveListTop - gap;
    if (lay.moveListHeight < 40) lay.moveListHeight = 40;

    return lay;
}

void Renderer::shutdown() {
    if (m_fontLarge) { TTF_CloseFont(m_fontLarge); m_fontLarge = nullptr; }
    if (m_fontMedium) { TTF_CloseFont(m_fontMedium); m_fontMedium = nullptr; }
    if (m_fontSmall) { TTF_CloseFont(m_fontSmall); m_fontSmall = nullptr; }
    if (m_fontPanel) { TTF_CloseFont(m_fontPanel); m_fontPanel = nullptr; }
    if (m_fontClock) { TTF_CloseFont(m_fontClock); m_fontClock = nullptr; }
    if (m_renderer) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    TTF_Quit();
    SDL_Quit();
}

void Renderer::beginFrame() {
    SDL_SetRenderDrawColor(m_renderer, 49, 46, 43, 255);
    SDL_RenderClear(m_renderer);
}

void Renderer::endFrame() {
    SDL_RenderPresent(m_renderer);
}

void Renderer::drawBoardGrid() {
    int left = m_boardLeft;
    int top = m_boardTop;
    int cs = m_cellSize;

    SDL_Rect boardRect = {left - cs/2 - 8, top - cs/2 - 8,
                          cs * 8 + cs + 16, cs * 9 + cs + 16};
    SDL_SetRenderDrawColor(m_renderer, 205, 170, 109, 255);
    SDL_RenderFillRect(m_renderer, &boardRect);

    SDL_SetRenderDrawColor(m_renderer, 80, 50, 20, 255);
    SDL_Rect border = {boardRect.x - 3, boardRect.y - 3, boardRect.w + 6, boardRect.h + 6};
    SDL_RenderDrawRect(m_renderer, &border);
    border = {border.x - 1, border.y - 1, border.w + 2, border.h + 2};
    SDL_RenderDrawRect(m_renderer, &border);

    SDL_SetRenderDrawColor(m_renderer, 60, 30, 10, 255);

    for (int r = 0; r < 10; r++) {
        int y = top + r * cs;
        SDL_RenderDrawLine(m_renderer, left, y, left + 8 * cs, y);
    }

    for (int c = 0; c < 9; c++) {
        if (c == 0 || c == 8) {
            SDL_RenderDrawLine(m_renderer, left + c * cs, top, left + c * cs, top + 9 * cs);
        } else {
            SDL_RenderDrawLine(m_renderer, left + c * cs, top, left + c * cs, top + 4 * cs);
            SDL_RenderDrawLine(m_renderer, left + c * cs, top + 5 * cs, left + c * cs, top + 9 * cs);
        }
    }

    SDL_RenderDrawLine(m_renderer, left + 3 * cs, top + 7 * cs, left + 5 * cs, top + 9 * cs);
    SDL_RenderDrawLine(m_renderer, left + 5 * cs, top + 7 * cs, left + 3 * cs, top + 9 * cs);
    SDL_RenderDrawLine(m_renderer, left + 3 * cs, top + 0 * cs, left + 5 * cs, top + 2 * cs);
    SDL_RenderDrawLine(m_renderer, left + 5 * cs, top + 0 * cs, left + 3 * cs, top + 2 * cs);

    if (m_fontMedium) {
        SDL_Color textCol = {60, 30, 10, 255};
        drawTextCentered("楚 河          汉 界", left + 4 * cs,
                         top + 4 * cs + cs / 2, textCol, m_fontMedium);
    }
}

void Renderer::drawBoardLabels(bool flipped, bool mirrored) {
    if (!m_fontSmall) return;
    SDL_Color col = {180, 160, 130, 255};
    const char* redLabels[] = {"九","八","七","六","五","四","三","二","一"};
    const char* blackLabels[] = {"1","2","3","4","5","6","7","8","9"};

    for (int c = 0; c < 9; c++) {
        int logC = mirrored ? (8 - c) : c;
        int dispC = flipped ? (8 - logC) : logC;
        int sx = m_boardLeft + c * m_cellSize;

        drawTextCentered(redLabels[dispC], sx, m_boardTop + 9 * m_cellSize + 18, col, m_fontSmall);
        drawTextCentered(blackLabels[dispC], sx, m_boardTop - 20, col, m_fontSmall);
    }
}

void Renderer::drawBoard(const Board& board, bool flipped, bool mirrored) {
    drawBoardGrid();
    drawBoardLabels(flipped, mirrored);
    drawAllPieces(board, flipped, mirrored);
}

const char* Renderer::pieceChar(PieceType type, Side side) const {
    if (side == Side::RED) {
        switch (type) {
            case PieceType::KING:    return "帅";
            case PieceType::ADVISOR: return "仕";
            case PieceType::BISHOP:  return "相";
            case PieceType::KNIGHT:  return "马";
            case PieceType::ROOK:    return "车";
            case PieceType::CANNON:  return "炮";
            case PieceType::PAWN:    return "兵";
            default: return "";
        }
    } else {
        switch (type) {
            case PieceType::KING:    return "将";
            case PieceType::ADVISOR: return "士";
            case PieceType::BISHOP:  return "象";
            case PieceType::KNIGHT:  return "马";
            case PieceType::ROOK:    return "车";
            case PieceType::CANNON:  return "炮";
            case PieceType::PAWN:    return "卒";
            default: return "";
        }
    }
}

SDL_Color Renderer::pieceColor(Side side) const {
    if (side == Side::RED) return {180, 30, 20, 255};
    return {20, 20, 20, 255};
}

SDL_Color Renderer::pieceBgColor(Side side) const {
    (void)side;
    return {250, 230, 180, 255};
}

void Renderer::drawPiece(int row, int col, Piece piece, bool flipped, bool mirrored) {
    if (piece.empty()) return;
    int sx, sy;
    boardToScreen(row, col, sx, sy, flipped, mirrored);

    int r = m_pieceRadius;

    SDL_Color bg = pieceBgColor(piece.side);
    filledCircleRGBA(m_renderer, sx, sy, r, bg.r, bg.g, bg.b, 255);

    SDL_Color pc = pieceColor(piece.side);
    aacircleRGBA(m_renderer, sx, sy, r, pc.r, pc.g, pc.b, 255);
    aacircleRGBA(m_renderer, sx, sy, r - 2, pc.r, pc.g, pc.b, 255);

    if (m_fontLarge) {
        drawTextCentered(pieceChar(piece.type, piece.side), sx, sy, pc, m_fontLarge);
    }
}

void Renderer::drawAllPieces(const Board& board, bool flipped, bool mirrored) {
    for (int r = 0; r < Board::ROWS; r++)
        for (int c = 0; c < Board::COLS; c++)
            drawPiece(r, c, board.get(r, c), flipped, mirrored);
}

void Renderer::drawSelection(int row, int col, bool flipped, bool mirrored) {
    int sx, sy;
    boardToScreen(row, col, sx, sy, flipped, mirrored);
    aacircleRGBA(m_renderer, sx, sy, m_pieceRadius + 3, 0, 120, 255, 200);
    aacircleRGBA(m_renderer, sx, sy, m_pieceRadius + 4, 0, 120, 255, 200);
}

void Renderer::drawLegalMoves(const std::vector<Move>& moves, bool flipped, bool mirrored) {
    for (auto& m : moves) {
        int sx, sy;
        boardToScreen(m.toRow, m.toCol, sx, sy, flipped, mirrored);
        if (m.captured.empty()) {
            filledCircleRGBA(m_renderer, sx, sy, 6, 0, 150, 0, 150);
        } else {
            aacircleRGBA(m_renderer, sx, sy, m_pieceRadius + 2, 200, 0, 0, 180);
            aacircleRGBA(m_renderer, sx, sy, m_pieceRadius + 3, 200, 0, 0, 180);
        }
    }
}

void Renderer::drawLastMove(const Move& move, bool flipped, bool mirrored) {
    int sx1, sy1, sx2, sy2;
    boardToScreen(move.fromRow, move.fromCol, sx1, sy1, flipped, mirrored);
    boardToScreen(move.toRow, move.toCol, sx2, sy2, flipped, mirrored);

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    int hs = m_cellSize / 3;
    SDL_Rect r1 = {sx1 - hs, sy1 - hs, hs * 2, hs * 2};
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 80);
    SDL_RenderFillRect(m_renderer, &r1);
    SDL_Rect r2 = {sx2 - hs, sy2 - hs, hs * 2, hs * 2};
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 0, 80);
    SDL_RenderFillRect(m_renderer, &r2);
}

void Renderer::drawPVArrow(const Move& move, Side side, bool active, bool flipped, bool mirrored) {
    int sx1, sy1, sx2, sy2;
    boardToScreen(move.fromRow, move.fromCol, sx1, sy1, flipped, mirrored);
    boardToScreen(move.toRow, move.toCol, sx2, sy2, flipped, mirrored);

    float dx = (float)(sx2 - sx1);
    float dy = (float)(sy2 - sy1);
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 2.0f) return;

    float ux = dx / len, uy = dy / len;
    float perx = -uy, pery = ux;

    float shrink = m_pieceRadius * 0.65f;
    float ax1 = sx1 + ux * shrink;
    float ay1 = sy1 + uy * shrink;
    float ax2 = sx2 - ux * shrink;
    float ay2 = sy2 - uy * shrink;

    float headLen   = m_cellSize * 0.28f;
    float headWidth = m_cellSize * 0.15f;
    int   thick     = std::max(2, m_cellSize / 15);

    float bx2 = ax2 - ux * headLen;
    float by2 = ay2 - uy * headLen;

    Uint8 r, g, b;
    Uint8 alpha = active ? 235 : 90;

    if (side == Side::RED) {
        r = 220; g = 20; b = 20;
    } else {
        r = 0; g = 0; b = 0;
    }

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    if (side == Side::BLACK) {
        Uint8 outA = active ? 160 : 55;
        for (int t = -(thick / 2 + 2); t <= (thick / 2 + 2); t++) {
            int ox = (int)(perx * t), oy = (int)(pery * t);
            SDL_SetRenderDrawColor(m_renderer, 230, 210, 170, outA);
            SDL_RenderDrawLine(m_renderer,
                (int)ax1 + ox, (int)ay1 + oy,
                (int)bx2 + ox, (int)by2 + oy);
        }
    }

    for (int t = -thick / 2; t <= thick / 2; t++) {
        int ox = (int)(perx * t), oy = (int)(pery * t);
        SDL_SetRenderDrawColor(m_renderer, r, g, b, alpha);
        SDL_RenderDrawLine(m_renderer,
            (int)ax1 + ox, (int)ay1 + oy,
            (int)bx2 + ox, (int)by2 + oy);
    }

    Sint16 tx  = (Sint16)ax2;
    Sint16 ty  = (Sint16)ay2;
    Sint16 hx1 = (Sint16)(ax2 - ux * headLen + perx * headWidth);
    Sint16 hy1 = (Sint16)(ay2 - uy * headLen + pery * headWidth);
    Sint16 hx2 = (Sint16)(ax2 - ux * headLen - perx * headWidth);
    Sint16 hy2 = (Sint16)(ay2 - uy * headLen - pery * headWidth);

    if (side == Side::BLACK) {
        Uint8 outA = active ? 160 : 55;
        Sint16 ohx1 = (Sint16)(ax2 - ux * headLen + perx * (headWidth + 2.5f));
        Sint16 ohy1 = (Sint16)(ay2 - uy * headLen + pery * (headWidth + 2.5f));
        Sint16 ohx2 = (Sint16)(ax2 - ux * headLen - perx * (headWidth + 2.5f));
        Sint16 ohy2 = (Sint16)(ay2 - uy * headLen - pery * (headWidth + 2.5f));
        filledTrigonRGBA(m_renderer, tx, ty, ohx1, ohy1, ohx2, ohy2, 230, 210, 170, outA);
    }

    filledTrigonRGBA(m_renderer, tx, ty, hx1, hy1, hx2, hy2, r, g, b, alpha);
}

bool Renderer::screenToBoard(int sx, int sy, int& row, int& col, bool flipped, bool mirrored) const {
    int cs = m_cellSize;
    int c = std::round((float)(sx - m_boardLeft) / cs);
    int r = std::round((float)(sy - m_boardTop) / cs);

    if (c < 0 || c > 8 || r < 0 || r > 9) return false;

    int cx = m_boardLeft + c * cs;
    int cy = m_boardTop + r * cs;
    int dist = std::abs(sx - cx) + std::abs(sy - cy);
    if (dist > cs / 2) return false;

    if (flipped) {
        row = r;
        col = mirrored ? c : (8 - c);
    } else {
        row = 9 - r;
        col = mirrored ? (8 - c) : c;
    }
    return true;
}

void Renderer::boardToScreen(int row, int col, int& sx, int& sy, bool flipped, bool mirrored) const {
    int dc = mirrored ? (8 - col) : col;
    if (flipped) {
        sx = m_boardLeft + (8 - dc) * m_cellSize;
        sy = m_boardTop + row * m_cellSize;
    } else {
        sx = m_boardLeft + dc * m_cellSize;
        sy = m_boardTop + (9 - row) * m_cellSize;
    }
}

void Renderer::drawText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* font) {
    if (!font) font = m_fontSmall;
    if (!font || text.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    if (tex) {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void Renderer::drawTextCentered(const std::string& text, int cx, int cy, SDL_Color color, TTF_Font* font) {
    if (!font) font = m_fontSmall;
    if (!font || text.empty()) return;

    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(m_renderer, surf);
    if (tex) {
        SDL_Rect dst = {cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
        SDL_RenderCopy(m_renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

void Renderer::drawMessage(const std::string& msg) {
    if (msg.empty()) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);

    int pw = 300, ph = 80;
    int px = (m_boardLeft + m_cellSize * 4) - pw / 2;
    int py = (m_boardTop + m_cellSize * 4) - ph / 2;

    SDL_Rect bg = {px, py, pw, ph};
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(m_renderer, &bg);
    SDL_SetRenderDrawColor(m_renderer, 200, 180, 100, 255);
    SDL_RenderDrawRect(m_renderer, &bg);

    SDL_Color yellow = {255, 220, 100, 255};
    drawTextCentered(msg, px + pw / 2, py + ph / 2, yellow, m_fontMedium);
}
