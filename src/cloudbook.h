#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

struct CloudBookMove {
    std::string move;   // UCI格式
    int score = 0;
    int winRate = 0;    // 百分比
    int drawRate = 0;
    int loseRate = 0;
    int totalGames = 0;
    std::string note;
};

class CloudBook {
public:
    using Callback = std::function<void(const std::vector<CloudBookMove>&, const std::string& error)>;

    CloudBook();
    ~CloudBook();

    // 查询云库
    void query(const std::string& fen, Callback cb);
    void cancelQuery();

    bool isQuerying() const { return m_querying; }
    void setEnabled(bool en) { m_enabled = en; }
    bool isEnabled() const { return m_enabled; }

private:

    std::atomic<bool> m_querying{false};
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_enabled{true};
    std::thread m_thread;
};
