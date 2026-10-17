#include "engine.h"
#include <unistd.h>
#include <signal.h>
#include <poll.h>
#include <sys/wait.h>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <iostream>

UCIEngine::UCIEngine() = default;

UCIEngine::~UCIEngine() {
    stop();
}

bool UCIEngine::start(const std::string& path) {
    // stop() 是幂等的，总是调用：即使上次引擎已死（m_running=false），
    // 残留的管道/读线程/僵尸进程也需要清理，否则 joinable 线程被覆盖会 terminate
    stop();

    m_options.clear();
    m_engineName.clear();
    m_skipNextBestMove = false;

    // 引擎死亡时向管道写入会触发 SIGPIPE 杀死整个 GUI，必须忽略
    signal(SIGPIPE, SIG_IGN);

    int toEngine[2], fromEngine[2];
    if (pipe(toEngine) < 0)
        return false;
    if (pipe(fromEngine) < 0) {
        close(toEngine[0]); close(toEngine[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(toEngine[0]); close(toEngine[1]);
        close(fromEngine[0]); close(fromEngine[1]);
        return false;
    }

    if (pid == 0) {
        // 子进程：父进程忽略了 SIGPIPE，信号屏蔽会跨 exec 继承，
        // 恢复默认避免引擎自身也忽略 SIGPIPE
        signal(SIGPIPE, SIG_DFL);
        close(toEngine[1]);
        close(fromEngine[0]);
        dup2(toEngine[0], STDIN_FILENO);
        dup2(fromEngine[1], STDOUT_FILENO);
        dup2(fromEngine[1], STDERR_FILENO);
        close(toEngine[0]);
        close(fromEngine[1]);
        execl(path.c_str(), path.c_str(), nullptr);
        _exit(1);
    }

    // 父进程
    close(toEngine[0]);
    close(fromEngine[1]);
    m_pipeIn[1] = toEngine[1];     // 写入引擎
    m_pipeOut[0] = fromEngine[0];  // 从引擎读取
    m_pid = pid;
    m_running = true;
    m_ready = false;

    m_readThread = std::thread(&UCIEngine::readLoop, this);

    // 发送 UCI 初始化
    sendUCI();
    return true;
}

void UCIEngine::stop() {
    // 不设早退：即使 m_running 已为 false（写失败检测到引擎死亡），
    // 仍要完成管道关闭、线程 join、子进程回收，保证幂等可重入
    m_running = false;
    m_thinking = false;

    sendQuit();

    // 关闭写端要持锁：sendCommand 也在写这同一个 fd，无锁关闭是 data race
    {
        std::lock_guard<std::mutex> lock(m_writeMtx);
        if (m_pipeIn[1] >= 0) { close(m_pipeIn[1]); m_pipeIn[1] = -1; }
    }

    // 先回收子进程：子进程退出后其 stdout 关闭，读线程的 read() 才返回。
    // 若先 join 再杀进程，引擎不响应 quit 时 read() 永不返回 → 死锁
    if (m_pid > 0) {
        int status = 0;
        // 返回 1=子进程已退出（含 ECHILD），0=仍在运行；EINTR 重试，
        // 不能把 -1 笼统当作"已退出"否则可能跳过回收导致僵尸/误杀复用 pid
        auto pollChild = [&]() -> int {
            while (true) {
                pid_t r = waitpid(m_pid, &status, WNOHANG);
                if (r == m_pid || (r < 0 && errno == ECHILD)) return 1;
                if (r == 0) return 0;
                if (errno != EINTR) return 1;
            }
        };
        if (pollChild() == 0) {
            // 引擎进程仍在运行，先礼后兵
            kill(m_pid, SIGTERM);
            bool alive = true;
            for (int i = 0; i < 10; i++) {
                usleep(10000);
                if (pollChild() != 0) { alive = false; break; }
            }
            if (alive) {
                kill(m_pid, SIGKILL);
                while (waitpid(m_pid, &status, 0) < 0 && errno == EINTR) {}
            }
        }
        m_pid = -1;
    }

    // 子进程已被回收 → 其 stdout 写端关闭 → 读线程 read() 返回 0 自然退出，
    // 先 join 再关闭 fd，消除与 readLoop 读 fd 的竞态
    if (m_readThread.joinable())
        m_readThread.join();

    if (m_pipeOut[0] >= 0) { close(m_pipeOut[0]); m_pipeOut[0] = -1; }
}

void UCIEngine::sendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(m_writeMtx); // fd 检查必须在锁内（stop() 持锁关闭）
    if (m_pipeIn[1] < 0) return;
    std::string line = cmd + "\n";
    const char* p = line.data();
    size_t left = line.size();
    while (left > 0) {
        ssize_t n = write(m_pipeIn[1], p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            // 管道断开（引擎已退出）：同步状态，避免后续误报 isRunning
            m_running = false;
            m_ready = false;
            m_thinking = false;
            return;
        }
        p += n;
        left -= (size_t)n;
    }
}

void UCIEngine::sendUCI() { sendCommand("uci"); }
void UCIEngine::sendIsReady() { sendCommand("isready"); }
void UCIEngine::sendNewGame() { sendCommand("ucinewgame"); }
void UCIEngine::sendQuit() { sendCommand("quit"); }
void UCIEngine::sendStop() { sendCommand("stop"); m_thinking = false; }

void UCIEngine::sendStopForRestart() {
    // 仅在确实思考中时才需要忽略旧 bestmove；否则标志会残留，
    // 吞掉下一次正常搜索的 bestmove 导致界面卡死
    if (m_thinking) m_skipNextBestMove = true;
    sendCommand("stop");
    // 不在这里把 m_thinking 设为 false，保留 true 功能不重要，
    // 反正 sendGo 会再次把它设为 true
}

void UCIEngine::sendPosition(const std::string& fen, const std::vector<std::string>& moves) {
    std::string cmd = "position fen " + fen;
    if (!moves.empty()) {
        cmd += " moves";
        for (auto& m : moves) cmd += " " + m;
    }
    sendCommand(cmd);
}

void UCIEngine::sendGo(int depth, int movetime, int wtime, int btime,
                       const std::vector<std::string>& searchmoves) {
    std::string cmd = "go";
    if (!searchmoves.empty()) {
        cmd += " searchmoves";
        for (const auto& m : searchmoves) cmd += " " + m;
    }
    if (depth > 0) cmd += " depth " + std::to_string(depth);
    if (movetime > 0) cmd += " movetime " + std::to_string(movetime);
    if (wtime > 0) cmd += " wtime " + std::to_string(wtime);
    if (btime > 0) cmd += " btime " + std::to_string(btime);
    if (depth == 0 && movetime == 0 && wtime == 0) cmd += " infinite";
    m_thinking = true;
    sendCommand(cmd);
}

void UCIEngine::sendSetOption(const std::string& name, const std::string& value) {
    std::string cmd = "setoption name " + name;
    if (!value.empty()) cmd += " value " + value;
    sendCommand(cmd);
}

void UCIEngine::readLoop() {
    char buf[4096];
    std::string pending;
    struct pollfd pfd;
    pfd.fd = m_pipeOut[0];
    pfd.events = POLLIN;

    while (m_running) {
        // poll 带超时而非阻塞 read：子进程被回收后若仍有继承 stdout 的
        // 孙进程存活，read() 永不返回会让 stop() 的 join 死锁；
        // 超时轮询保证线程总能随 m_running=false 退出
        int pr = poll(&pfd, 1, 100);
        if (pr == 0) continue;          // 超时，重查 m_running
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }
        ssize_t n = read(pfd.fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = 0;
        pending += buf;

        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending = pending.substr(pos + 1);
            parseLine(line);
        }
    }
    // 引擎进程意外退出时重置思考标志
    m_thinking = false;
}

void UCIEngine::parseLine(const std::string& line) {
    std::istringstream ss(line);
    std::string token;
    ss >> token;

    if (token == "uciok") {
        m_ready = false;
        sendIsReady();
    }
    else if (token == "readyok") {
        m_ready = true;
    }
    else if (token == "id") {
        std::string sub;
        ss >> sub;
        if (sub == "name") {
            std::getline(ss, m_engineName);
            if (!m_engineName.empty() && m_engineName[0] == ' ')
                m_engineName = m_engineName.substr(1);
        }
    }
    else if (token == "option") {
        EngineOption opt;
        std::string key;
        while (ss >> key) {
            if (key == "name") {
                std::string namePart;
                while (ss >> namePart) {
                    if (namePart == "type") { ss >> opt.type; break; }
                    if (!opt.name.empty()) opt.name += " ";
                    opt.name += namePart;
                }
            } else if (key == "default") {
                ss >> opt.defaultVal;
                opt.value = opt.defaultVal;
            } else if (key == "min") {
                ss >> opt.min;
            } else if (key == "max") {
                ss >> opt.max;
            } else if (key == "var") {
                std::string v;
                ss >> v;
                opt.comboVars.push_back(v);
            }
        }
        if (!opt.name.empty())
            m_options.push_back(opt);
    }
    else if (token == "info") {
        EngineInfo info;
        std::string key;
        while (ss >> key) {
            if (key == "depth") ss >> info.depth;
            else if (key == "score") {
                std::string sub;
                ss >> sub;
                if (sub == "cp") ss >> info.score;
                else if (sub == "mate") {
                    ss >> info.mateIn;
                    info.isMate = true;
                }
            }
            else if (key == "nodes") ss >> info.nodes;
            else if (key == "nps") ss >> info.nps;
            else if (key == "pv") {
                std::string rest;
                std::getline(ss, rest);
                if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);
                info.pv = rest;
                break;
            }
        }
        if (m_infoCb) m_infoCb(info);
    }
    else if (token == "bestmove") {
        std::string bm;
        ss >> bm;
        if (m_skipNextBestMove.exchange(false)) {
            // 是重启前的当前搜索的 bestmove，不重置 m_thinking，不回调
            return;
        }
        m_thinking = false;
        if (m_bestMoveCb) m_bestMoveCb(bm);
    }
}
