#pragma once
#include "board.h"
#include "engine.h"
#include "cloudbook.h"
#include "renderer.h"
#include "rules.h"
#include <string>
#include <vector>
#include <mutex>
#include <fstream>

enum class GameMode {
    HUMAN_VS_ENGINE, // 人机对战
    FREE_PLAY        // 自由摆棋
};

enum class UIState {
    NORMAL,
    SETTINGS,
    GAME_OVER,
    FILE_BROWSER
};

struct GameSettings {
    std::string enginePath;
    std::string nnuePath;
    int searchDepth = 18;
    int searchTime = 5000; // ms
    int threads = 2;
    int hashSize = 128;    // MB
    bool useCloudBook = true;
    bool cloudBookFirst = true; // 优先使用云库
    bool enginePlaysRed = false;   // 引擎执红
    bool enginePlaysBlack = true;  // 引擎执黑
    bool flipBoard = false;
    bool mirrorBoard = false;  // 左右镜像
};

class GUI {
public:
    GUI();
    ~GUI();

    bool init();
    void run();

private:
    void handleEvent(SDL_Event& ev);
    void handleMouseDown(int x, int y);
    void handleMouseUp(int x, int y);
    void handleMouseMove(int x, int y);
    void handleKeyDown(SDL_Keycode key);

    void update();
    void render();

    // 游戏逻辑
    void newGame();
    void makeHumanMove(int fromR, int fromC, int toR, int toC);
    void startEngineMove();
    void tryStartEngineMove(); // 如果轮到引擎走，启动引擎
    void onEngineBestMove(const std::string& uci);
    void onEngineInfo(const EngineInfo& info);
    void onCloudBookResult(const std::vector<CloudBookMove>& moves);
    void undoLastMove();
    bool isEngineTurn() const;

    // 引擎设置
    void applyEngineSettings();
    void loadEngine(const std::string& path);
    void openEngineFileDialog();
    void initButtons();
    void updateButtons(int mx, int my);
    int hitButton(int x, int y);
    void handleResize(int w, int h);

    // UCI走法转中文
    static std::string uciToChineseNotation(const std::string& uci, const Board& board);

    // 自动检测引擎路径
    std::string detectEnginePath();
    std::string detectNNUEPath();

    // 配置文件
    std::string configFilePath() const;
    void loadConfig();
    void saveConfig();

    Board m_board;
    UCIEngine m_engine;
    CloudBook m_cloudBook;
    Renderer m_renderer;

    GameMode m_gameMode = GameMode::HUMAN_VS_ENGINE;
    UIState m_uiState = UIState::NORMAL;
    GameSettings m_settings;

    // 选子状态
    int m_selRow = -1, m_selCol = -1;
    std::vector<Move> m_legalMoves;
    bool m_hasLastMove = false;
    Move m_lastMove;

    // 引擎信息
    std::string m_engineInfoText;
    std::vector<CloudBookMove> m_cloudMoves;
    std::mutex m_infoMutex;

    // 走法历史 (用于悔棋)
    struct MoveRecord {
        Move move;
        Side side;
        std::string uci;       // UCI 格式 (如 "h2e2")
        std::string chinese;   // 中文记谱 (如 "炮二平五")
        std::string fenBefore; // 走棋前的 FEN
    };
    std::vector<MoveRecord> m_moveHistory;

    // 规则系统
    RuleEngine m_rules;
    int m_noCaptureCount = 0;  // 连续无吃子步数
    // 走棋后检查规则，返回true表示游戏继续，false表示游戏结束(和/负)
    bool checkGameRules();

    // 棋谱浏览
    int m_viewIndex = -1;       // -1 = 实时局面, >=0 = 浏览历史第N步后的局面
    int m_moveListScroll = 0;   // 棋谱列表滚动偏移
    Board m_viewBoard;          // 浏览模式下显示的棋盘

    // 消息
    std::string m_message;
    Uint32 m_messageTime = 0;

    // UI
    std::vector<Renderer::Button> m_buttons;
    bool m_running = false;

    // 设置面板
    int m_settingsScroll = 0;
    std::vector<std::string> m_settingsLines;

    // 文件浏览器
    std::string m_fbCurrentDir;
    std::vector<std::string> m_fbEntries; // 目录项目
    int m_fbScroll = 0;
    int m_fbSelected = -1;
    void fbNavigate(const std::string& dir);
    void fbHandleClick(int x, int y);
    void fbHandleScroll(int dy);

    // PV转换辅助
    std::string pvToReadable(const std::string& pv) const;

    // 棋谱功能
    void navigateTo(int index);       // 跳转到第index步后的局面, -1=实时
    void navigateForward();
    void navigateBackward();
    void exportPGN(const std::string& path);
    void importPGN(const std::string& path);
    void openPGNExportDialog();
    void openPGNImportDialog();
    int moveListHitTest(int x, int y) const; // 点击棋谱列表返回走法索引
    void moveListHandleScroll(int dy);

    // 获取当前显示的棋盘（浏览模式或实时）
    const Board& displayBoard() const;
    bool isViewingHistory() const { return m_viewIndex >= 0; }

    // 窗口大小
    int m_windowW = 1050;
    int m_windowH = 680;
};
