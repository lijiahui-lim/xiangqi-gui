#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include "board.h"
#include <string>
#include <vector>

struct CloudBookMove; // forward declare

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(int windowW, int windowH);
    void shutdown();
    void recalcLayout(int windowW, int windowH);
    void reloadFonts(int cellSize);

    void beginFrame();
    void endFrame();

    SDL_Window* window() const { return m_window; }
    SDL_Renderer* renderer() const { return m_renderer; }

    // 棋盘绘制
    void drawBoard(const Board& board, bool flipped, bool mirrored = false);
    void drawPiece(int row, int col, Piece piece, bool flipped, bool mirrored = false);
    void drawAllPieces(const Board& board, bool flipped, bool mirrored = false);

    // 选中和高亮
    void drawSelection(int row, int col, bool flipped, bool mirrored = false);
    void drawLegalMoves(const std::vector<Move>& moves, bool flipped, bool mirrored = false);
    void drawLastMove(const Move& move, bool flipped, bool mirrored = false);

    // 侧边栏信息
    void drawSidePanel(const std::string& engineName, const std::string& infoText,
                       const std::vector<CloudBookMove>& cloudMoves,
                       Side currentSide, bool engineThinking);

    // 按钮
    struct Button {
        SDL_Rect rect;
        std::string label;
        bool hovered = false;
    };
    void drawButton(const Button& btn);
    void drawButtons(const std::vector<Button>& buttons);

    // 引擎设置面板
    void drawSettingsPanel(const std::string& title, const std::vector<std::string>& lines,
                           int scrollOffset, bool visible);

    // 文件浏览器
    struct FileBrowserState {
        std::string currentDir;
        std::vector<std::string> entries;
        int scrollOffset = 0;
        int selectedIndex = -1;
        bool visible = false;
    };
    void drawFileBrowser(const FileBrowserState& state);
    // 返回点击的条目索引，-1表示未点击条目
    int fileBrowserHitTest(int x, int y, const FileBrowserState& state) const;

    // 坐标转换
    bool screenToBoard(int sx, int sy, int& row, int& col, bool flipped, bool mirrored = false) const;
    void boardToScreen(int row, int col, int& sx, int& sy, bool flipped, bool mirrored = false) const;

    // 棋盘区域
    int boardLeft() const { return m_boardLeft; }
    int boardTop() const { return m_boardTop; }
    int cellSize() const { return m_cellSize; }
    int windowW() const { return m_windowW; }
    int windowH() const { return m_windowH; }
    int panelLeft() const { return m_panelLeft; }

    // 绘制文字
    void drawText(const std::string& text, int x, int y, SDL_Color color, TTF_Font* font = nullptr);
    void drawTextCentered(const std::string& text, int cx, int cy, SDL_Color color, TTF_Font* font = nullptr);

    // 绘制消息弹窗
    void drawMessage(const std::string& msg);

    // 棋谱列表
    struct MoveListEntry {
        int roundNum;          // 回合数 (1-based)
        std::string redMove;   // 红方走法
        std::string blackMove; // 黑方走法
    };
    void drawMoveList(const std::vector<MoveListEntry>& entries, int scrollOffset,
                      int highlightIndex, int areaTop, int areaHeight);
    // 返回点击的走法在m_moveHistory中的索引, -1=未命中
    int moveListHitTest(int x, int y, int numMoves, int scrollOffset,
                        int areaTop, int areaHeight) const;

    // 面板布局分区（按钮区高度和棋谱区位置由此统一计算）
    struct PanelLayout {
        int infoTop;       // 引擎信息区顶部
        int infoHeight;    // 引擎信息区高度
        int moveListTop;   // 棋谱列表区顶部
        int moveListHeight;// 棋谱列表区高度
        int buttonsTop;    // 按钮区顶部
        int buttonsHeight; // 按钮区高度
    };
    PanelLayout calcPanelLayout() const;

private:
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_fontLarge = nullptr;   // 棋子文字
    TTF_Font* m_fontMedium = nullptr;  // 标题
    TTF_Font* m_fontSmall = nullptr;   // 面板正文
    TTF_Font* m_fontPanel = nullptr;   // 面板小字
    std::string m_fontPath;

    int m_windowW, m_windowH;
    int m_boardLeft, m_boardTop;
    int m_cellSize;
    int m_pieceRadius;
    int m_panelLeft;

    void drawBoardGrid();
    void drawBoardLabels(bool flipped, bool mirrored = false);

    // 棋子文字
    const char* pieceChar(PieceType type, Side side) const;
    SDL_Color pieceColor(Side side) const;
    SDL_Color pieceBgColor(Side side) const;
};
