#include "renderer.h"
#include <SDL2/SDL2_gfxPrimitives.h>

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
