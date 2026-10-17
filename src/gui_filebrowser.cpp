#include "gui.h"
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

void GUI::loadEngine(const std::string& path) {
    if (path.empty()) return;
    if (m_engine.isRunning()) {
        m_engine.sendStop();
        m_engine.stop();
    }

    if (m_engine.start(path)) {
        m_settings.enginePath = path;

        // 自动查找 nnue：先引擎同目录，再逐级向上（兼容 皮卡鱼/Linux/ 这类
        // 引擎在子目录、权重在父目录的布局）；目录不可读时跳过该级
        for (fs::path dir = fs::path(path).parent_path(); !dir.empty(); dir = dir.parent_path()) {
            bool found = false;
            try {
                for (auto& entry : fs::directory_iterator(dir)) {
                    if (entry.path().extension() == ".nnue") {
                        m_settings.nnuePath = entry.path().string();
                        found = true;
                        break;
                    }
                }
            } catch (...) {}
            if (found || dir == dir.parent_path()) break; // 找到或已到根目录
        }

        applyEngineSettings();
        m_engine.sendNewGame();
        m_engine.sendIsReady();
        m_message = "引擎已加载: " + fs::path(path).filename().string();
        saveConfig();
    } else {
        m_message = "引擎加载失败!";
    }
    m_messageTime = SDL_GetTicks();
}

void GUI::openEngineFileDialog() {
    // 打开内建文件浏览器
    m_uiState = UIState::FILE_BROWSER;
    m_fbScroll = 0;
    m_fbSelected = -1;
    // 从引擎所在目录开始，或从 /home/li/中国象棋 开始
    std::string startDir = "/home/li/中国象棋";
    if (!m_settings.enginePath.empty()) {
        startDir = fs::path(m_settings.enginePath).parent_path().string();
    }
    fbNavigate(startDir);
}

void GUI::fbNavigate(const std::string& dir) {
    m_fbCurrentDir = dir;
    m_fbEntries.clear();
    m_fbScroll = 0;
    m_fbSelected = -1;

    // 返回上级
    if (dir != "/") {
        m_fbEntries.push_back("../");
    }

    try {
        std::vector<std::string> dirs, files;
        for (auto& entry : fs::directory_iterator(dir)) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue; // 隐藏文件
            bool isDir = false;
            try {
                isDir = entry.is_directory();
            } catch (...) { continue; } // 坏符号链接等：跳过单个条目而非整列失败
            if (isDir) {
                dirs.push_back(name + "/");
            } else {
                // 只显示可执行文件和目录
                try {
                    auto perms = entry.status().permissions();
                    bool isExec = (perms & fs::perms::owner_exec) != fs::perms::none ||
                                  (perms & fs::perms::group_exec) != fs::perms::none ||
                                  (perms & fs::perms::others_exec) != fs::perms::none;
                    if (isExec || name.find("pikafish") != std::string::npos ||
                        name.find("engine") != std::string::npos) {
                        files.push_back(name);
                    }
                } catch (...) { continue; }
            }
        }
        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());
        m_fbEntries.insert(m_fbEntries.end(), dirs.begin(), dirs.end());
        m_fbEntries.insert(m_fbEntries.end(), files.begin(), files.end());
    } catch (...) {
        m_fbEntries.push_back("(无法读取目录)");
    }
}

void GUI::fbHandleClick(int x, int y) {
    Renderer::FileBrowserState state;
    state.currentDir = m_fbCurrentDir;
    state.entries = m_fbEntries;
    state.scrollOffset = m_fbScroll;
    state.selectedIndex = m_fbSelected;
    state.visible = true;

    int idx = m_renderer.fileBrowserHitTest(x, y, state);
    if (idx < 0) {
        // 点击面板外关闭（尺寸公式必须与 drawFileBrowser 一致，
        // 否则宽窗口下面板两侧的内部点击会被误判为外部）
        int pw = std::min(m_windowW - 40, std::max(600, m_windowW * 2 / 3));
        int ph = std::min(m_windowH - 40, std::max(500, m_windowH * 3 / 4));
        int px = (m_windowW - pw) / 2;
        int py = (m_windowH - ph) / 2;
        if (x < px || x > px + pw || y < py || y > py + ph) {
            m_uiState = UIState::NORMAL;
        }
        return;
    }

    m_fbSelected = idx;
    std::string entry = m_fbEntries[idx];
    if (entry == "../") {
        // 上级目录
        fs::path parent = fs::path(m_fbCurrentDir).parent_path();
        fbNavigate(parent.string());
    } else if (!entry.empty() && entry.back() == '/') {
        // 进入子目录
        std::string sub = entry.substr(0, entry.size() - 1);
        fbNavigate((fs::path(m_fbCurrentDir) / sub).string());
    } else if (entry.find("(") == std::string::npos) {
        // 选择文件
        std::string fullPath = (fs::path(m_fbCurrentDir) / entry).string();
        m_uiState = UIState::NORMAL;
        loadEngine(fullPath);
    }
}

void GUI::fbHandleScroll(int dy) {
    m_fbScroll -= dy;
    m_fbScroll = std::max(0, std::min(m_fbScroll, (int)m_fbEntries.size() - 5));
}
