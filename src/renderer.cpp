#include "renderer.h"
#include "cloudbook.h"
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

    // drawBoardGrid 中棋盘背景是 [boardLeft - cs/2 - boardPad, ...]
    const int boardPad    = 8;
    const int CLOCK_GAP   = 6;   // 时钟右边缘 → 棋盘背景左边缘
    const int PANEL_GAP   = 8;   // 棋盘背景右边缘 → 面板左边缘
    const int RIGHT_MARGIN = 10;
    const int MIN_PANEL_W  = 180;

    // 时钟字体大小（与 reloadFonts 中保持一致）
    int panelBase0  = std::max(m_windowH / 50, 10);
    int clockFontSz = std::max(panelBase0 + 3, 13);
    // 时钟列宽 = 容纳「步时 MM:SS」最宽行 + 左色条 + 左右内边距
    // "步时 " 约 2.5字符宽，时间串约 2字符宽，共约 4.5 字符
    m_clockW = std::max(44, clockFontSz * 5 + 16);

    // cellSize 受高度限制，同时也受「保留最小面板后的剩余宽度」限制
    int cellFromH = (windowH - 40) / 10;
    int availW    = windowW - m_clockW - CLOCK_GAP - PANEL_GAP - MIN_PANEL_W - RIGHT_MARGIN;
    int cellFromW = (availW - boardPad * 2) / 9;
    m_cellSize    = std::max(20, std::min(cellFromH, cellFromW));
    m_pieceRadius = m_cellSize * 2 / 5;

    // 棋盘背景视觉宽：cellSize*9 + boardPad*2（含左右各半格+pad）
    int boardVisualW    = m_cellSize * 9 + boardPad * 2;
    int boardVisualLeft = m_clockW + CLOCK_GAP;          // 紧贴时钟，无额外空白

    // boardLeft = 网格第一条竖线（drawBoardGrid 的 left 变量）
    m_boardLeft = boardVisualLeft + boardPad + m_cellSize / 2;
    m_boardTop  = std::max(15, (windowH - m_cellSize * 9) / 2);

    // 面板紧接棋盘背景右侧，填满剩余空间（无居中留白）
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

    // 棋子字体随cellSize缩放
    int largeSz = std::max(12, cellSize * 7 / 10);
    // 面板字体随窗口高度等比缩放（不设硬上限）
    int panelBase = std::max(m_windowH / 50, 10);
    int medSz = std::max(panelBase + 4, 14);
    int smallSz = std::max(panelBase + 1, 11);
    int panelSz = std::max(panelBase, 9);
    // 时钟标签字体：比面板字体大，加粗
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
    int totalH = panelBottom - panelTop;

    // 按钮区：5行, 按钮高度和间距按窗口高度等比缩放
    int bh = std::max(24, m_windowH / 23);
    int gap = std::max(4, bh / 5);
    int btnRows = 6;
    lay.buttonsHeight = btnRows * bh + (btnRows - 1) * gap;
    lay.buttonsTop = panelBottom - lay.buttonsHeight;

    // 信息区和棋谱区平分按钮上方空间
    int contentH = lay.buttonsTop - panelTop - gap;
    lay.infoTop = panelTop;
    lay.infoHeight = std::max(80, contentH * 45 / 100); // 45% 给引擎+云库
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
    // 背景色 - 深木色
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

    // 棋盘背景 - 木纹色
    SDL_Rect boardRect = {left - cs/2 - 8, top - cs/2 - 8,
                          cs * 8 + cs + 16, cs * 9 + cs + 16};
    SDL_SetRenderDrawColor(m_renderer, 205, 170, 109, 255);
    SDL_RenderFillRect(m_renderer, &boardRect);

    // 边框
    SDL_SetRenderDrawColor(m_renderer, 80, 50, 20, 255);
    SDL_Rect border = {boardRect.x - 3, boardRect.y - 3, boardRect.w + 6, boardRect.h + 6};
    SDL_RenderDrawRect(m_renderer, &border);
    border = {border.x - 1, border.y - 1, border.w + 2, border.h + 2};
    SDL_RenderDrawRect(m_renderer, &border);

    SDL_SetRenderDrawColor(m_renderer, 60, 30, 10, 255);

    // 横线
    for (int r = 0; r < 10; r++) {
        int y = top + r * cs;
        SDL_RenderDrawLine(m_renderer, left, y, left + 8 * cs, y);
    }

    // 竖线 - 上半和下半分开画（河界断开）
    for (int c = 0; c < 9; c++) {
        if (c == 0 || c == 8) {
            // 两边的线贯穿
            SDL_RenderDrawLine(m_renderer, left + c * cs, top, left + c * cs, top + 9 * cs);
        } else {
            // 上半部分
            SDL_RenderDrawLine(m_renderer, left + c * cs, top, left + c * cs, top + 4 * cs);
            // 下半部分
            SDL_RenderDrawLine(m_renderer, left + c * cs, top + 5 * cs, left + c * cs, top + 9 * cs);
        }
    }

    // 九宫斜线
    // 下方九宫 (row 0-2, 即屏幕底部)
    SDL_RenderDrawLine(m_renderer, left + 3 * cs, top + 7 * cs, left + 5 * cs, top + 9 * cs);
    SDL_RenderDrawLine(m_renderer, left + 5 * cs, top + 7 * cs, left + 3 * cs, top + 9 * cs);
    // 上方九宫
    SDL_RenderDrawLine(m_renderer, left + 3 * cs, top + 0 * cs, left + 5 * cs, top + 2 * cs);
    SDL_RenderDrawLine(m_renderer, left + 5 * cs, top + 0 * cs, left + 3 * cs, top + 2 * cs);

    // "楚河汉界" 文字
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

        // 底部红方标签
        drawTextCentered(redLabels[dispC], sx, m_boardTop + 9 * m_cellSize + 18, col, m_fontSmall);
        // 顶部黑方标签
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

    // 棋子底色圆
    SDL_Color bg = pieceBgColor(piece.side);
    filledCircleRGBA(m_renderer, sx, sy, r, bg.r, bg.g, bg.b, 255);

    // 外圈
    SDL_Color pc = pieceColor(piece.side);
    aacircleRGBA(m_renderer, sx, sy, r, pc.r, pc.g, pc.b, 255);
    aacircleRGBA(m_renderer, sx, sy, r - 2, pc.r, pc.g, pc.b, 255);

    // 棋子文字
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

void Renderer::drawSidePanel(const std::string& engineName, const std::string& infoText,
                              const std::vector<CloudBookMove>& cloudMoves,
                              Side currentSide, bool engineThinking) {
    int px = m_panelLeft;
    int pw = m_windowW - px - 15;
    auto lay = calcPanelLayout();

    // 面板背景
    SDL_Rect panelBg = {px - 5, lay.infoTop - 5, pw + 10, m_windowH - 20};
    SDL_SetRenderDrawColor(m_renderer, 60, 56, 52, 255);
    SDL_RenderFillRect(m_renderer, &panelBg);

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color yellow = {255, 220, 100, 255};
    SDL_Color green = {100, 220, 100, 255};
    SDL_Color gray = {160, 160, 160, 255};

    // 设置裁剪区域——只在 infoTop ~ infoTop+infoHeight 范围内绘制
    SDL_Rect infoClip = {px - 5, lay.infoTop, pw + 10, lay.infoHeight};
    SDL_RenderSetClipRect(m_renderer, &infoClip);

    int py = lay.infoTop;
    int lineH = std::max(18, m_windowH / 32); // 行高与字体同步缩放

    // 引擎名
    if (m_fontMedium) {
        drawText(engineName.empty() ? "未连接引擎" : engineName, px, py, yellow, m_fontMedium);
        py += lineH + 8;
    }

    // 当前走棋方
    if (m_fontSmall) {
        std::string sideStr = (currentSide == Side::RED) ? "红方走棋" : "黑方走棋";
        if (engineThinking) sideStr += " (思考中...)";
        drawText(sideStr, px, py, white, m_fontSmall);
        py += lineH + 4;

        // 分隔线
        SDL_SetRenderDrawColor(m_renderer, 100, 96, 92, 255);
        SDL_RenderDrawLine(m_renderer, px, py, px + pw, py);
        py += 6;

        // 引擎信息
        drawText("引擎分析:", px, py, yellow, m_fontSmall);
        py += lineH + 2;

        // 逐行显示引擎信息
        std::string remaining = infoText;
        int lineCount = 0;
        int maxInfoLines = std::max(2, (lay.infoHeight - (py - lay.infoTop)) / lineH / 2);
        while (!remaining.empty() && lineCount < maxInfoLines) {
            auto nl = remaining.find('\n');
            std::string line = (nl != std::string::npos) ?
                remaining.substr(0, nl) : remaining;
            remaining = (nl != std::string::npos) ? remaining.substr(nl + 1) : "";
            drawText(line, px + 5, py, white, m_fontSmall);
            py += lineH;
            lineCount++;
        }

        py += 6;
        SDL_SetRenderDrawColor(m_renderer, 100, 96, 92, 255);
        SDL_RenderDrawLine(m_renderer, px, py, px + pw, py);
        py += 6;

        // 云库信息
        drawText("云库走法:", px, py, yellow, m_fontSmall);
        py += lineH + 2;

        if (cloudMoves.empty()) {
            drawText("无云库数据", px + 5, py, gray, m_fontSmall);
        } else {
            int count = 0;
            int maxCloud = std::max(2, (lay.infoTop + lay.infoHeight - py) / lineH);
            for (auto& cm : cloudMoves) {
                if (count >= maxCloud) break;
                char buf[256];
                // note字段已包含人类可读走法名, move为UCI格式备用
                std::string displayName = cm.note.empty() ? cm.move : cm.note;
                // 格式化分数显示
                std::string scoreStr;
                if (cm.score > 900)
                    scoreStr = "绝杀";
                else if (cm.score > 300)
                    scoreStr = "大优(" + std::to_string(cm.score) + ")";
                else if (cm.score > 100)
                    scoreStr = "优势(" + std::to_string(cm.score) + ")";
                else if (cm.score > 30)
                    scoreStr = "略优(" + std::to_string(cm.score) + ")";
                else if (cm.score > -30)
                    scoreStr = "均势(" + std::to_string(cm.score) + ")";
                else if (cm.score > -100)
                    scoreStr = "略劣(" + std::to_string(cm.score) + ")";
                else if (cm.score > -300)
                    scoreStr = "劣势(" + std::to_string(cm.score) + ")";
                else if (cm.score > -900)
                    scoreStr = "大劣(" + std::to_string(cm.score) + ")";
                else
                    scoreStr = "必败(" + std::to_string(cm.score) + ")";

                snprintf(buf, sizeof(buf), "%-6s  %s",
                         displayName.c_str(), scoreStr.c_str());
                drawText(buf, px + 5, py, green, m_fontSmall);
                py += lineH;
                count++;
            }
        }
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);
}

void Renderer::drawButton(const Button& btn) {
    SDL_Color bg = btn.hovered ?
        SDL_Color{80, 120, 180, 255} : SDL_Color{70, 66, 62, 255};
    SDL_SetRenderDrawColor(m_renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(m_renderer, &btn.rect);

    SDL_SetRenderDrawColor(m_renderer, 120, 116, 112, 255);
    SDL_RenderDrawRect(m_renderer, &btn.rect);

    if (m_fontSmall) {
        SDL_Color white = {220, 220, 220, 255};
        drawTextCentered(btn.label, btn.rect.x + btn.rect.w / 2,
                         btn.rect.y + btn.rect.h / 2, white, m_fontSmall);
    }
}

void Renderer::drawButtons(const std::vector<Button>& buttons) {
    for (auto& b : buttons)
        drawButton(b);
}

void Renderer::drawSettingsPanel(const std::string& title, const std::vector<std::string>& lines,
                                  int scrollOffset, bool visible) {
    if (!visible) return;

    // 半透明遮罩
    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, m_windowW, m_windowH};
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(m_renderer, &overlay);

    // 面板
    int pw = std::min(m_windowW - 40, std::max(500, m_windowW * 2 / 3));
    int ph = std::min(m_windowH - 40, std::max(500, m_windowH * 3 / 4));
    int px = (m_windowW - pw) / 2, py = (m_windowH - ph) / 2;
    SDL_Rect panel = {px, py, pw, ph};
    SDL_SetRenderDrawColor(m_renderer, 50, 46, 42, 255);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawColor(m_renderer, 120, 116, 112, 255);
    SDL_RenderDrawRect(m_renderer, &panel);

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color yellow = {255, 220, 100, 255};

    if (m_fontMedium) {
        drawTextCentered(title, px + pw / 2, py + 20, yellow, m_fontMedium);
    }

    if (m_fontSmall) {
        int lineH = std::max(18, m_windowH / 32);
        int ly = py + 50 - scrollOffset;
        for (size_t i = 0; i < lines.size() && ly < py + ph - 20; i++) {
            if (ly >= py + 40)
                drawText(lines[i], px + 15, ly, white, m_fontSmall);
            ly += lineH;
        }
    }
}

bool Renderer::screenToBoard(int sx, int sy, int& row, int& col, bool flipped, bool mirrored) const {
    int cs = m_cellSize;
    int c = std::round((float)(sx - m_boardLeft) / cs);
    int r = std::round((float)(sy - m_boardTop) / cs);

    if (c < 0 || c > 8 || r < 0 || r > 9) return false;

    // 检查是否在交叉点附近
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

void Renderer::drawFileBrowser(const FileBrowserState& state) {
    if (!state.visible) return;

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, m_windowW, m_windowH};
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 160);
    SDL_RenderFillRect(m_renderer, &overlay);

    int pw = std::min(m_windowW - 40, std::max(600, m_windowW * 2 / 3));
    int ph = std::min(m_windowH - 40, std::max(500, m_windowH * 3 / 4));
    int px = (m_windowW - pw) / 2;
    int py = (m_windowH - ph) / 2;

    // 面板背景
    SDL_Rect panel = {px, py, pw, ph};
    SDL_SetRenderDrawColor(m_renderer, 45, 42, 38, 255);
    SDL_RenderFillRect(m_renderer, &panel);
    SDL_SetRenderDrawColor(m_renderer, 140, 130, 110, 255);
    SDL_RenderDrawRect(m_renderer, &panel);

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color yellow = {255, 220, 100, 255};
    SDL_Color cyan = {100, 200, 255, 255};
    SDL_Color gray = {160, 160, 160, 255};

    if (m_fontMedium) {
        drawTextCentered("选择引擎文件", px + pw / 2, py + 16, yellow, m_fontMedium);
    }

    // 当前路径
    TTF_Font* font = m_fontPanel ? m_fontPanel : m_fontSmall;
    if (font) {
        drawText(state.currentDir, px + 10, py + 38, gray, font);
    }

    // 条目列表
    int itemH = std::max(20, m_windowH / 28);
    int listTop = py + 60;
    int listH = ph - 70;
    int maxVisible = listH / itemH;

    SDL_Rect listClip = {px + 2, listTop, pw - 4, listH};
    SDL_RenderSetClipRect(m_renderer, &listClip);

    for (int i = 0; i < (int)state.entries.size() && i < state.scrollOffset + maxVisible; i++) {
        if (i < state.scrollOffset) continue;
        int iy = listTop + (i - state.scrollOffset) * itemH;

        // 选中高亮
        if (i == state.selectedIndex) {
            SDL_Rect sel = {px + 4, iy, pw - 8, itemH};
            SDL_SetRenderDrawColor(m_renderer, 60, 90, 140, 255);
            SDL_RenderFillRect(m_renderer, &sel);
        }

        if (font) {
            bool isDir = !state.entries[i].empty() && state.entries[i].back() == '/';
            drawText(state.entries[i], px + 12, iy + 2,
                     isDir ? cyan : white, font);
        }
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);
}

int Renderer::fileBrowserHitTest(int x, int y, const FileBrowserState& state) const {
    if (!state.visible) return -1;

    int pw = std::min(m_windowW - 40, std::max(600, m_windowW * 2 / 3));
    int ph = std::min(m_windowH - 40, std::max(500, m_windowH * 3 / 4));
    int px = (m_windowW - pw) / 2;
    int py = (m_windowH - ph) / 2;
    int listTop = py + 60;
    int itemH = std::max(20, m_windowH / 28);

    if (x < px + 4 || x > px + pw - 4 || y < listTop || y > py + ph - 10)
        return -1;

    int idx = (y - listTop) / itemH + state.scrollOffset;
    if (idx >= 0 && idx < (int)state.entries.size())
        return idx;
    return -1;
}

void Renderer::drawMoveList(const std::vector<MoveListEntry>& entries, int scrollOffset,
                             int highlightIndex, int areaTop, int areaHeight) {
    int px = m_panelLeft;
    int pw = m_windowW - px - 15;
    int itemH = std::max(18, m_windowH / 32);
    int maxVisible = areaHeight / itemH;

    TTF_Font* font = m_fontPanel ? m_fontPanel : m_fontSmall;
    if (!font) return;

    SDL_Color white = {220, 220, 220, 255};
    SDL_Color yellow = {255, 220, 100, 255};
    SDL_Color hiCol = {60, 100, 160, 255};
    SDL_Color redCol = {220, 80, 60, 255};
    SDL_Color blackCol = {180, 180, 180, 255};

    // 标题
    drawText("棋谱:", px, areaTop, yellow, m_fontSmall);
    int listTop = areaTop + itemH;

    // 设置裁剪区域
    SDL_Rect clip = {px, listTop, pw, areaHeight - itemH};
    SDL_RenderSetClipRect(m_renderer, &clip);

    for (int i = 0; i < (int)entries.size() && i < scrollOffset + maxVisible; i++) {
        if (i < scrollOffset) continue;
        int iy = listTop + (i - scrollOffset) * itemH;
        auto& e = entries[i];

        // 高亮：highlightIndex对应m_moveHistory索引
        // 红方走法索引: i*2, 黑方走法索引: i*2+1
        bool hlRed = (highlightIndex == i * 2);
        bool hlBlack = (highlightIndex == i * 2 + 1);

        if (hlRed || hlBlack) {
            SDL_Rect hlRect = {px, iy, pw, itemH};
            SDL_SetRenderDrawColor(m_renderer, hiCol.r, hiCol.g, hiCol.b, 255);
            SDL_RenderFillRect(m_renderer, &hlRect);
        }

        // 回合号
        char buf[16];
        snprintf(buf, sizeof(buf), "%d.", e.roundNum);
        drawText(buf, px + 2, iy + 1, white, font);

        // 红方走法
        int redX = px + std::max(30, itemH * 3 / 2);
        drawText(e.redMove, redX, iy + 1, hlRed ? yellow : redCol, font);

        // 黑方走法
        int blackX = px + pw / 2 + 10;
        if (!e.blackMove.empty()) {
            drawText(e.blackMove, blackX, iy + 1, hlBlack ? yellow : blackCol, font);
        }
    }

    SDL_RenderSetClipRect(m_renderer, nullptr);
}

int Renderer::moveListHitTest(int x, int y, int numMoves, int scrollOffset,
                               int areaTop, int areaHeight) const {
    int px = m_panelLeft;
    int pw = m_windowW - px - 15;
    int itemH = std::max(18, m_windowH / 32);
    int listTop = areaTop + itemH;

    if (x < px || x > px + pw || y < listTop || y > areaTop + areaHeight)
        return -1;

    int row = (y - listTop) / itemH + scrollOffset;
    int numRows = (numMoves + 1) / 2;
    if (row < 0 || row >= numRows)
        return -1;

    // 判断点击的是红方还是黑方
    int midX = px + pw / 2 + 5;
    if (x < midX) {
        // 红方
        return row * 2;
    } else {
        // 黑方
        int idx = row * 2 + 1;
        if (idx < numMoves) return idx;
        return row * 2; // 黑方空则回退到红方
    }
}

// -------- 计时器卡片（左侧上下两端） --------
static std::string fmtTime(Uint32 ms) {
    int s = (int)(ms / 1000);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
    return buf;
}

void Renderer::drawClockPanel(Uint32 redTotalMs, Uint32 redLastMs,
                               Uint32 blackTotalMs, Uint32 blackLastMs,
                               Side currentSide, bool flipped, bool mirrored)
{
    if (!m_fontPanel || !m_fontSmall) return;
    int cw = m_clockW - 6;  // 卡片内容宽度
    if (cw < 36) return;

    int lineH = std::max(13, m_windowH / 46);
    int lineGap = std::max(4, lineH / 3);   // 行间额外间距
    int cardH = lineH * 3 + lineGap * 2 + 14;
    int cardW = cw;

    // 时钟始终在棋盘左侧，镜像不影响位置
    (void)mirrored;
    int cx   = 3;
    int topY = m_boardTop;
    int botY = m_boardTop + 9 * m_cellSize - cardH;

    auto drawCard = [&](int x, int y, const char* label,
                        Uint32 totalMs, Uint32 lastMs,
                        SDL_Color sideCol, bool isActive)
    {
        SDL_Rect bg = {x, y, cardW, cardH};
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer,
            isActive ? 50 : 45, isActive ? 45 : 42, isActive ? 35 : 40,
            isActive ? 230 : 200);
        SDL_RenderFillRect(m_renderer, &bg);

        // 左侧色条
        SDL_Rect bar = {x, y + 3, 3, cardH - 6};
        SDL_SetRenderDrawColor(m_renderer, sideCol.r, sideCol.g, sideCol.b,
                               isActive ? 255 : 100);
        SDL_RenderFillRect(m_renderer, &bar);

        if (isActive) {
            SDL_SetRenderDrawColor(m_renderer, sideCol.r, sideCol.g, sideCol.b, 180);
            SDL_RenderDrawRect(m_renderer, &bg);
        }

        int tx = x + 7;
        int ty = y + 4;

        SDL_Color nameCol = isActive ? SDL_Color{255,255,255,255}
                                     : SDL_Color{150,150,150,255};
        drawText(label, tx, ty, nameCol, m_fontClock ? m_fontClock : m_fontSmall);
        ty += lineH + lineGap;

        SDL_Color timeCol = isActive ? SDL_Color{255,225,80,255}
                                     : SDL_Color{185,185,185,255};
        drawText("局时 " + fmtTime(totalMs), tx, ty, timeCol, m_fontSmall);
        ty += lineH + lineGap;

        SDL_Color stepCol = {110, 110, 110, 255};
        drawText("步时 " + fmtTime(lastMs), tx, ty, stepCol, m_fontPanel);
    };

    SDL_Color redSideCol   = {200, 60,  50,  255};
    SDL_Color blackSideCol = {100, 100, 100, 255};

    if (!flipped) {
        // 上黑下红
        drawCard(cx, topY, "黑方", blackTotalMs, blackLastMs,
                 blackSideCol, currentSide == Side::BLACK);
        drawCard(cx, botY, "红方", redTotalMs, redLastMs,
                 redSideCol, currentSide == Side::RED);
    } else {
        drawCard(cx, topY, "红方", redTotalMs, redLastMs,
                 redSideCol, currentSide == Side::RED);
        drawCard(cx, botY, "黑方", blackTotalMs, blackLastMs,
                 blackSideCol, currentSide == Side::BLACK);
    }
}

// -------- 棋局编辑面板 --------
// 棋子顺序（0-6红，7-13黑）
static const PieceType kPaletteTypes[] = {
    PieceType::KING, PieceType::ADVISOR, PieceType::BISHOP,
    PieceType::KNIGHT, PieceType::ROOK, PieceType::CANNON, PieceType::PAWN
};

void Renderer::drawEditPalette(const Piece& selected, Side firstSide)
{
    auto lay = calcPanelLayout();
    int px = m_panelLeft;
    int pw = m_windowW - px - 15;
    int py = lay.infoTop;

    SDL_Color yellow = {255, 220, 100, 255};
    SDL_Color white  = {220, 220, 220, 255};
    SDL_Color gray   = {140, 140, 140, 255};

    // 标题
    if (m_fontSmall)
        drawText("棋局编辑", px, py, yellow, m_fontSmall);
    py += std::max(18, m_windowH / 32) + 6;

    // 每个棋子按钮半径
    int pr = std::max(14, std::min(m_pieceRadius, pw / 16));
    int spacing = pr * 2 + 6;
    int rowPad = pr + 3;

    m_editPieceRects.clear();
    m_editPieceRects.resize(15); // 0..13 pieces, 14 eraser

    // 红方一行（7个棋子）
    for (int i = 0; i < 7; i++) {
        int cx = px + rowPad + i * spacing;
        int cy = py + pr;
        Piece p{kPaletteTypes[i], Side::RED};
        bool isSel = !selected.empty() && selected.type == p.type && selected.side == p.side;

        SDL_Color bg    = isSel ? SDL_Color{240, 200, 80, 255} : SDL_Color{250, 230, 180, 255};
        SDL_Color pcol  = {180, 30, 20, 255};
        filledCircleRGBA(m_renderer, cx, cy, pr, bg.r, bg.g, bg.b, 255);
        aacircleRGBA(m_renderer, cx, cy, pr, pcol.r, pcol.g, pcol.b, 255);
        if (isSel) aacircleRGBA(m_renderer, cx, cy, pr + 2, 255, 200, 0, 255);
        if (m_fontPanel) drawTextCentered(pieceChar(p.type, p.side), cx, cy, pcol, m_fontPanel);

        m_editPieceRects[i] = {cx - pr, cy - pr, pr * 2, pr * 2};
    }
    py += pr * 2 + 8;

    // 黑方一行（7个棋子）
    for (int i = 0; i < 7; i++) {
        int cx = px + rowPad + i * spacing;
        int cy = py + pr;
        Piece p{kPaletteTypes[i], Side::BLACK};
        bool isSel = !selected.empty() && selected.type == p.type && selected.side == p.side;

        SDL_Color bg   = isSel ? SDL_Color{240, 200, 80, 255} : SDL_Color{250, 230, 180, 255};
        SDL_Color pcol = {20, 20, 20, 255};
        filledCircleRGBA(m_renderer, cx, cy, pr, bg.r, bg.g, bg.b, 255);
        aacircleRGBA(m_renderer, cx, cy, pr, pcol.r, pcol.g, pcol.b, 255);
        if (isSel) aacircleRGBA(m_renderer, cx, cy, pr + 2, 255, 200, 0, 255);
        if (m_fontPanel) drawTextCentered(pieceChar(p.type, p.side), cx, cy, pcol, m_fontPanel);

        m_editPieceRects[7 + i] = {cx - pr, cy - pr, pr * 2, pr * 2};
    }
    py += pr * 2 + 8;

    // 橡皮擦
    {
        int cx = px + rowPad + pr;
        int cy = py + pr;
        bool isSel = selected.empty();
        SDL_Color bg = isSel ? SDL_Color{240, 200, 80, 255} : SDL_Color{80, 76, 72, 255};
        filledCircleRGBA(m_renderer, cx, cy, pr, bg.r, bg.g, bg.b, 255);
        aacircleRGBA(m_renderer, cx, cy, pr, 160, 160, 160, 255);
        if (isSel) aacircleRGBA(m_renderer, cx, cy, pr + 2, 255, 200, 0, 255);
        if (m_fontPanel) drawTextCentered("擦", cx, cy, white, m_fontPanel);
        m_editPieceRects[14] = {cx - pr, cy - pr, pr * 2, pr * 2};
        py += pr * 2 + 8;
    }

    // 分隔线
    SDL_SetRenderDrawColor(m_renderer, 100, 96, 92, 255);
    SDL_RenderDrawLine(m_renderer, px, py, px + pw, py);
    py += 6;

    // 操作按钮
    int bh = std::max(22, m_windowH / 26);
    int bw = std::max(50, (pw - 8) / 3);
    int gap = std::max(3, (pw - bw * 3) / 2);

    auto drawBtn = [&](SDL_Rect r, const char* lbl) {
        SDL_SetRenderDrawColor(m_renderer, 70, 66, 62, 255);
        SDL_RenderFillRect(m_renderer, &r);
        SDL_SetRenderDrawColor(m_renderer, 120, 116, 112, 255);
        SDL_RenderDrawRect(m_renderer, &r);
        if (m_fontPanel)
            drawTextCentered(lbl, r.x + r.w / 2, r.y + r.h / 2, white, m_fontPanel);
    };

    m_editClearRect = {px,               py, bw, bh};
    m_editResetRect = {px + bw + gap,     py, bw, bh};
    m_editDoneRect  = {px + (bw + gap)*2, py, bw, bh};

    drawBtn(m_editClearRect, "清空");
    drawBtn(m_editResetRect, "还原");
    drawBtn(m_editDoneRect,  "完成");
    py += bh + 8;

    // 先行方选择：红先 / 黑先
    int labelW = 0;
    if (m_fontPanel) {
        int tw = 0, th = 0;
        TTF_SizeUTF8(m_fontPanel, "先行:", &tw, &th);
        labelW = tw + 4;
        drawText("先行:", px, py + (bh - th) / 2, gray, m_fontPanel);
    }
    int btnW2  = std::max(50, (pw - labelW - gap) / 2);
    m_editRedFirstRect   = {px + labelW,               py, btnW2, bh};
    m_editBlackFirstRect = {px + labelW + btnW2 + gap,  py, btnW2, bh};

    // 红先按钮
    {
        bool active = (firstSide == Side::RED);
        SDL_Rect r = m_editRedFirstRect;
        SDL_SetRenderDrawColor(m_renderer, active ? 160 : 70, active ? 40 : 66, active ? 40 : 62, 255);
        SDL_RenderFillRect(m_renderer, &r);
        SDL_SetRenderDrawColor(m_renderer, active ? 220 : 120, active ? 80 : 116, active ? 80 : 112, 255);
        SDL_RenderDrawRect(m_renderer, &r);
        SDL_Color tc = active ? SDL_Color{255,200,200,255} : SDL_Color{180,180,180,255};
        if (m_fontPanel) drawTextCentered("红先", r.x + r.w/2, r.y + r.h/2, tc, m_fontPanel);
    }
    // 黑先按钮
    {
        bool active = (firstSide == Side::BLACK);
        SDL_Rect r = m_editBlackFirstRect;
        SDL_SetRenderDrawColor(m_renderer, active ? 50 : 70, active ? 50 : 66, active ? 50 : 62, 255);
        SDL_RenderFillRect(m_renderer, &r);
        SDL_SetRenderDrawColor(m_renderer, active ? 140 : 120, active ? 140 : 116, active ? 140 : 112, 255);
        SDL_RenderDrawRect(m_renderer, &r);
        SDL_Color tc = active ? SDL_Color{220,220,255,255} : SDL_Color{180,180,180,255};
        if (m_fontPanel) drawTextCentered("黑先", r.x + r.w/2, r.y + r.h/2, tc, m_fontPanel);
    }
    py += bh + 8;

    // 提示文字
    if (m_fontPanel) {
        drawText("左键:放置  右键:删除", px, py, gray, m_fontPanel);
    }
}

int Renderer::editPaletteHitTest(int x, int y) const
{
    // 棋子 0..14
    for (int i = 0; i < (int)m_editPieceRects.size(); i++) {
        const auto& r = m_editPieceRects[i];
        if (r.w > 0) {
            SDL_Point p{x, y};
            SDL_Rect rr = r;
            // enlarge hit area slightly
            rr.x -= 3; rr.y -= 3; rr.w += 6; rr.h += 6;
            if (SDL_PointInRect(&p, &rr)) return i;
        }
    }
    // 操作按钮
    SDL_Point p{x, y};
    if (SDL_PointInRect(&p, &m_editClearRect)) return 15;
    if (SDL_PointInRect(&p, &m_editResetRect)) return 16;
    if (SDL_PointInRect(&p, &m_editDoneRect))  return 17;
    if (SDL_PointInRect(&p, &m_editRedFirstRect))   return 18;
    if (SDL_PointInRect(&p, &m_editBlackFirstRect)) return 19;
    return -1;
}
