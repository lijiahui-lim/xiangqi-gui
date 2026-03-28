#pragma once
#include <string>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>

struct EngineInfo {
    int depth = 0;
    int score = 0;       // 分数（厘兵）
    bool isMate = false;
    int mateIn = 0;
    long long nodes = 0;
    int nps = 0;
    std::string pv;      // 主要变例
    std::string bestMove;
};

struct EngineOption {
    std::string name;
    std::string type;   // "spin", "check", "string", "combo", "button"
    std::string value;
    std::string defaultVal;
    int min = 0, max = 0;
    std::vector<std::string> comboVars;
};

class UCIEngine {
public:
    using InfoCallback = std::function<void(const EngineInfo&)>;
    using BestMoveCallback = std::function<void(const std::string&)>;

    UCIEngine();
    ~UCIEngine();

    bool start(const std::string& path);
    void stop();
    bool isRunning() const { return m_running; }

    // UCI 命令
    void sendUCI();
    void sendIsReady();
    void sendNewGame();
    void sendPosition(const std::string& fen, const std::vector<std::string>& moves = {});
    void sendGo(int depth = 0, int movetime = 0, int wtime = 0, int btime = 0);
    void sendStop();
    void sendStopForRestart();  // 将要重新分析时调用，使 bestmove 回复不重置 m_thinking
    void sendQuit();
    void sendSetOption(const std::string& name, const std::string& value);

    // 回调
    void setInfoCallback(InfoCallback cb) { m_infoCb = cb; }
    void setBestMoveCallback(BestMoveCallback cb) { m_bestMoveCb = cb; }

    // 获取引擎选项
    const std::vector<EngineOption>& getOptions() const { return m_options; }
    std::string getEngineName() const { return m_engineName; }

    bool isReady() const { return m_ready; }
    bool isThinking() const { return m_thinking; }

private:
    void readLoop();
    void parseLine(const std::string& line);
    void sendCommand(const std::string& cmd);

    int m_pipeIn[2] = {-1, -1};   // engine stdin
    int m_pipeOut[2] = {-1, -1};  // engine stdout
    pid_t m_pid = -1;

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_ready{false};
    std::atomic<bool> m_thinking{false};
    std::atomic<bool> m_skipNextBestMove{false};  // 重启分析时忽略旧 bestmove
    std::thread m_readThread;

    InfoCallback m_infoCb;
    BestMoveCallback m_bestMoveCb;

    std::vector<EngineOption> m_options;
    std::string m_engineName;
    std::mutex m_writeMtx;
};
