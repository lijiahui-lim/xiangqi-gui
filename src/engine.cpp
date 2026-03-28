#include "engine.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <sstream>
#include <iostream>

UCIEngine::UCIEngine() = default;

UCIEngine::~UCIEngine() {
    stop();
}

bool UCIEngine::start(const std::string& path) {
    if (m_running) stop();

    int toEngine[2], fromEngine[2];
    if (pipe(toEngine) < 0 || pipe(fromEngine) < 0)
        return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(toEngine[0]); close(toEngine[1]);
        close(fromEngine[0]); close(fromEngine[1]);
        return false;
    }

    if (pid == 0) {
        // 子进程
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
    if (!m_running) return;
    m_running = false;

    sendQuit();

    if (m_pipeIn[1] >= 0) { close(m_pipeIn[1]); m_pipeIn[1] = -1; }
    if (m_pipeOut[0] >= 0) { close(m_pipeOut[0]); m_pipeOut[0] = -1; }

    if (m_readThread.joinable())
        m_readThread.join();

    if (m_pid > 0) {
        int status;
        waitpid(m_pid, &status, WNOHANG);
        if (!WIFEXITED(status)) {
            kill(m_pid, SIGTERM);
            usleep(100000);
            kill(m_pid, SIGKILL);
            waitpid(m_pid, &status, 0);
        }
        m_pid = -1;
    }
}

void UCIEngine::sendCommand(const std::string& cmd) {
    if (m_pipeIn[1] < 0) return;
    std::lock_guard<std::mutex> lock(m_writeMtx);
    std::string line = cmd + "\n";
    write(m_pipeIn[1], line.c_str(), line.size());
}

void UCIEngine::sendUCI() { sendCommand("uci"); }
void UCIEngine::sendIsReady() { sendCommand("isready"); }
void UCIEngine::sendNewGame() { sendCommand("ucinewgame"); }
void UCIEngine::sendQuit() { sendCommand("quit"); }
void UCIEngine::sendStop() { sendCommand("stop"); m_thinking = false; }

void UCIEngine::sendStopForRestart() {
    // 设标志：下一个 bestmove 不重置 m_thinking
    m_skipNextBestMove = true;
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

void UCIEngine::sendGo(int depth, int movetime, int wtime, int btime) {
    std::string cmd = "go";
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

    while (m_running && m_pipeOut[0] >= 0) {
        ssize_t n = read(m_pipeOut[0], buf, sizeof(buf) - 1);
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
