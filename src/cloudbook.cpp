#include "cloudbook.h"
#include <curl/curl.h>
#include <sstream>
#include <cstring>

CloudBook::CloudBook() {}

CloudBook::~CloudBook() {
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    m_querying = false;
}

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// curl 进度回调：返回非 0 中断传输（用于真正的取消）
static int progressCallback(void* userp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic<bool>*>(userp)->load() ? 1 : 0;
}

void CloudBook::query(const std::string& fen, Callback cb) {
    cancelQuery();
    m_cancel = false;
    m_querying = true;

    m_thread = std::thread([this, fen, cb]() {
        std::vector<CloudBookMove> results;
        std::string error;

        CURL* curl = curl_easy_init();
        if (!curl) {
            error = "云库初始化失败";
            m_querying = false;
            if (cb) cb(results, error);
            return;
        }

        // 使用 chessdb.cn 云库 API
        char* encodedFen = curl_easy_escape(curl, fen.c_str(), (int)fen.size());
        std::string url = "https://www.chessdb.cn/chessdb.php?action=queryall&board=" +
                          std::string(encodedFen ? encodedFen : "");
        if (encodedFen) curl_free(encodedFen);

        std::string response;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "XiangqiGUI/1.0");
        // 让 m_cancel 能真正中断传输，否则 cancelQuery 的 join 会阻塞至超时
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &m_cancel);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCallback);

        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
            error = std::string("Error: ") + curl_easy_strerror(res);
        else if (response.empty())
            error = "云库返回为空";
        else if (response.find("move:") == std::string::npos)
            error = response;

        if (res == CURLE_OK && !m_cancel && !response.empty() && response[0] != 'u') {
            // chessdb 返回格式: move:h2e2,score:36,rank:2,note:...|move:b0c2,...
            // 按 | 分割
            std::istringstream ss(response);
            std::string entry;
            while (std::getline(ss, entry, '|')) {
                if (entry.empty() || entry.find("move:") == std::string::npos) continue;
                CloudBookMove cbm;
                std::istringstream es(entry);
                std::string field;
                while (std::getline(es, field, ',')) {
                    auto sep = field.find(':');
                    if (sep == std::string::npos) continue;
                    std::string key = field.substr(0, sep);
                    std::string val = field.substr(sep + 1);
                    if (key == "move") cbm.move = val;
                    else if (key == "score") {
                        try { cbm.score = std::stoi(val); } catch (...) {}
                    }
                    else if (key == "winrate") {
                        try { cbm.winRate = std::stoi(val); } catch (...) {}
                    }
                    else if (key == "note") cbm.note = val;
                }
                if (!cbm.move.empty())
                    results.push_back(cbm);
            }
            // 仅当解析到有效招法时清空错误；否则保留上面的返回文本错误。
            if (!results.empty()) {
                error.clear();
            }
        }

        m_querying = false;
        if (!m_cancel && cb) cb(results, error);
    });
}


void CloudBook::cancelQuery() {
    m_cancel = true;
    if (m_thread.joinable())
        m_thread.join();
    m_querying = false;
}
