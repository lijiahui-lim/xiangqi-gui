#include "renderer.h"
#include "cloudbook.h"
#include <cstdio>
#include <algorithm>

void Renderer::drawSidePanel(const std::string& engineName, const std::string& infoText,
                              const std::vector<CloudBookMove>& cloudMoves,
                              const std::string& cloudErrorText,
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

    // 设置裁剪区域
    SDL_Rect infoClip = {px - 5, lay.infoTop, pw + 10, lay.infoHeight};
    SDL_RenderSetClipRect(m_renderer, &infoClip);

    int py = lay.infoTop;
    int lineH = std::max(18, m_windowH / 32);

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

        SDL_SetRenderDrawColor(m_renderer, 100, 96, 92, 255);
        SDL_RenderDrawLine(m_renderer, px, py, px + pw, py);
        py += 6;

        drawText("引擎分析:", px, py, yellow, m_fontSmall);
        py += lineH + 2;

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

        drawText("云库走法:", px, py, yellow, m_fontSmall);
        py += lineH + 2;

        if (!cloudErrorText.empty()) {
            drawText(cloudErrorText, px + 5, py, SDL_Color{255, 120, 120, 255}, m_fontSmall);
        } else if (cloudMoves.empty()) {
            drawText("无云库数据", px + 5, py, gray, m_fontSmall);
        } else {
            int count = 0;
            int maxCloud = std::max(2, (lay.infoTop + lay.infoHeight - py) / lineH);
            for (auto& cm : cloudMoves) {
                if (count >= maxCloud) break;
                char buf[256];
                std::string displayName = cm.note.empty() ? cm.move : cm.note;
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

    SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, m_windowW, m_windowH};
    SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 150);
    SDL_RenderFillRect(m_renderer, &overlay);

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

    TTF_Font* font = m_fontPanel ? m_fontPanel : m_fontSmall;
    if (font) {
        drawText(state.currentDir, px + 10, py + 38, gray, font);
    }

    int itemH = std::max(20, m_windowH / 28);
    int listTop = py + 60;
    int listH = ph - 70;
    int maxVisible = listH / itemH;

    SDL_Rect listClip = {px + 2, listTop, pw - 4, listH};
    SDL_RenderSetClipRect(m_renderer, &listClip);

    for (int i = 0; i < (int)state.entries.size() && i < state.scrollOffset + maxVisible; i++) {
        if (i < state.scrollOffset) continue;
        int iy = listTop + (i - state.scrollOffset) * itemH;

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

    drawText("棋谱:", px, areaTop, yellow, m_fontSmall);
    int listTop = areaTop + itemH;

    SDL_Rect clip = {px, listTop, pw, areaHeight - itemH};
    SDL_RenderSetClipRect(m_renderer, &clip);

    for (int i = 0; i < (int)entries.size() && i < scrollOffset + maxVisible; i++) {
        if (i < scrollOffset) continue;
        int iy = listTop + (i - scrollOffset) * itemH;
        auto& e = entries[i];

        bool hlRed = (highlightIndex == i * 2);
        bool hlBlack = (highlightIndex == i * 2 + 1);

        if (hlRed || hlBlack) {
            SDL_Rect hlRect = {px, iy, pw, itemH};
            SDL_SetRenderDrawColor(m_renderer, hiCol.r, hiCol.g, hiCol.b, 255);
            SDL_RenderFillRect(m_renderer, &hlRect);
        }

        char buf[16];
        snprintf(buf, sizeof(buf), "%d.", e.roundNum);
        drawText(buf, px + 2, iy + 1, white, font);

        int redX = px + std::max(30, itemH * 3 / 2);
        drawText(e.redMove, redX, iy + 1, hlRed ? yellow : redCol, font);

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

    int midX = px + pw / 2 + 5;
    if (x < midX) {
        return row * 2;
    } else {
        int idx = row * 2 + 1;
        if (idx < numMoves) return idx;
        return row * 2;
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
    int cw = m_clockW - 6;
    if (cw < 36) return;

    int lineH = std::max(13, m_windowH / 46);
    int lineGap = std::max(4, lineH / 3);
    int cardH = lineH * 3 + lineGap * 2 + 14;
    int cardW = cw;

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
