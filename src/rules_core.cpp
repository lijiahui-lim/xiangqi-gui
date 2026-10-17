#include "rules.h"
#include "board.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace {
std::string trim(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && std::isspace((unsigned char)s[b])) b++;
    size_t e = s.size();
    while (e > b && std::isspace((unsigned char)s[e - 1])) e--;
    return s.substr(b, e - b);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string unquote(const std::string& s) {
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

const char* kDefaultRuleScript = R"RULES(# 中国象棋规则脚本
# 语法:
#   rule "规则名"
#   when <条件表达式>
#   action <动作>
#
# 条件变量:
#   in_check, has_legal_moves, no_capture_count, move_count, current_side, last_captured
#   perpetual_check_count       — 连续将军循环中局面重复次数
#   position_repeat_count       — 当前局面总重复次数（含当前）
#
# 动作:
#   checkmate  — 被将死/困毙
#   draw       — 和棋
#   lose       — 当前方负
#   win "消息" — 当前方胜
#   warn "消息" — 显示提示

# 终局规则必须排在 warn 规则之前（evaluate 返回首个匹配的规则，
# warn 在前会把同时成立的将死/困毙/和棋判定遮蔽掉）

rule "被将军必须应将"
when in_check and (not has_legal_moves)
action checkmate

rule "困毙"
when (not in_check) and (not has_legal_moves)
action checkmate

rule "三次重复和棋"
when position_repeat_count >= 3
action draw

rule "60步不吃子和棋"
when no_capture_count >= 120
action draw

rule "循环长将警告"
when in_check and perpetual_check_count >= 2
action warn "循环长将! 请变招!"

rule "将军提示"
when in_check and has_legal_moves
action warn "将军! 必须应将!"
)RULES";

bool isCmpOp(const std::string& s) {
    return s == "==" || s == "!=" || s == ">" || s == "<" || s == ">=" || s == "<=";
}
} // namespace

void RuleEngine::loadFromFile(const std::string& path) {
    m_filePath = path;

    std::string content;
    {
        std::ifstream ifs(path);
        if (ifs.is_open()) {
            std::ostringstream oss;
            oss << ifs.rdbuf();
            content = oss.str();
        }
    }

    if (content.empty()) {
        content = kDefaultRuleScript;
        std::ofstream ofs(path);
        if (ofs.is_open()) ofs << content;
    }

    auto parsed = parseFile(content);
    if (parsed.empty()) {
        content = kDefaultRuleScript;
        std::ofstream ofs(path);
        if (ofs.is_open()) ofs << content;
        parsed = parseFile(content);
    }

    m_rules = std::move(parsed);
}

void RuleEngine::reloadFromFile() {
    if (!m_filePath.empty()) {
        loadFromFile(m_filePath);
        try { m_lastModTime = std::filesystem::last_write_time(m_filePath); } catch (...) {}
    }
}

void RuleEngine::reloadIfChanged() {
    if (m_filePath.empty()) return;
    try {
        auto curMod = std::filesystem::last_write_time(m_filePath);
        if (curMod != m_lastModTime) {
            loadFromFile(m_filePath);
            m_lastModTime = curMod;
        }
    } catch (...) {}
}

RuleResult RuleEngine::evaluate(const GameState& state) const {
    for (const auto& r : m_rules) {
        CondNode cond = parseCondition(r.conditionStr);
        if (!evalCond(cond, state)) continue;

        std::string msg;
        RuleAction action = parseAction(r.actionStr, msg);
        RuleResult rr;
        rr.action = action;
        rr.message = msg;
        rr.ruleName = r.name;
        return rr;
    }

    return {};
}

std::vector<ParsedRule> RuleEngine::parseFile(const std::string& content) {
    std::vector<ParsedRule> out;
    std::istringstream iss(content);
    std::string line;

    ParsedRule cur;
    bool hasRule = false;

    auto flushRule = [&]() {
        if (hasRule && !cur.name.empty() && !cur.conditionStr.empty() && !cur.actionStr.empty()) {
            out.push_back(cur);
        }
        cur = ParsedRule{};
        hasRule = false;
    };

    while (std::getline(iss, line)) {
        std::string t = trim(line);
        // 行内注释：剥掉引号外 '#' 之后的内容。条件解析要求表达式被完整
        // 消费（fail-closed），"when ... # 注释" 会残留 token 被判语法错误
        bool inQ = false;
        for (size_t i = 0; i < t.size(); i++) {
            if (t[i] == '"') inQ = !inQ;
            else if (t[i] == '#' && !inQ) { t = trim(t.substr(0, i)); break; }
        }
        if (t.empty() || t[0] == '#') continue;

        if (startsWith(t, "rule ")) {
            flushRule();
            hasRule = true;
            cur.name = unquote(trim(t.substr(5)));
            continue;
        }
        if (startsWith(t, "when ")) {
            if (hasRule) cur.conditionStr = trim(t.substr(5));
            continue;
        }
        if (startsWith(t, "action ")) {
            if (hasRule) cur.actionStr = trim(t.substr(7));
            continue;
        }
    }

    flushRule();
    return out;
}

CondNode RuleEngine::parseCondition(const std::string& expr) const {
    auto t = tokenize(expr);
    size_t p = 0;
    CondNode node = pOr(t, p);
    // 表达式必须被完整消费：残留 token 说明有语法错误（如 "a = b"、"a b"），
    // 此时 fail-closed（条件恒不成立），避免部分解析导致规则误触发
    if (p >= t.size() || t[p].type != Tok::END) {
        CondNode bad;
        bad.type = CondNode::BOOL_LIT;
        bad.bval = false;
        return bad;
    }
    return node;
}

RuleAction RuleEngine::parseAction(const std::string& s, std::string& msg) const {
    std::string t = trim(s);
    if (t == "checkmate") return RuleAction::CHECKMATE;
    if (t == "draw") return RuleAction::DRAW;
    if (t == "lose") return RuleAction::LOSE;
    if (startsWith(t, "win")) {
        msg = unquote(trim(t.substr(3)));
        return RuleAction::WIN;
    }
    if (startsWith(t, "warn")) {
        msg = unquote(trim(t.substr(4)));
        return RuleAction::WARN;
    }
    return RuleAction::NONE;
}

bool RuleEngine::evalCond(const CondNode& n, const GameState& st) const {
    switch (n.type) {
        case CondNode::BOOL_LIT:
            return n.bval;
        case CondNode::VAR:
            return getVar(n.sval, st) != 0;
        case CondNode::NOT_OP:
            return n.children.empty() ? false : !evalCond(n.children[0], st);
        case CondNode::AND:
            return n.children.size() >= 2 ? (evalCond(n.children[0], st) && evalCond(n.children[1], st)) : false;
        case CondNode::OR:
            return n.children.size() >= 2 ? (evalCond(n.children[0], st) || evalCond(n.children[1], st)) : false;
        case CondNode::CMP: {
            if (n.children.size() < 2) return false;
            const CondNode& l = n.children[0];
            const CondNode& r = n.children[1];

            bool lStr = (l.type == CondNode::STR) || (l.type == CondNode::VAR && l.sval == "current_side");
            bool rStr = (r.type == CondNode::STR) || (r.type == CondNode::VAR && r.sval == "current_side");

            if (lStr || rStr) {
                auto sideToStr = [&](const CondNode& x) {
                    if (x.type == CondNode::STR) return x.sval;
                    if (x.type == CondNode::VAR && x.sval == "current_side") return std::string(st.currentSide == 0 ? "red" : "black");
                    return std::string();
                };
                std::string ls = sideToStr(l);
                std::string rs = sideToStr(r);
                if (n.sval == "==") return ls == rs;
                if (n.sval == "!=") return ls != rs;
                if (n.sval == ">") return ls > rs;
                if (n.sval == "<") return ls < rs;
                if (n.sval == ">=") return ls >= rs;
                if (n.sval == "<=") return ls <= rs;
                return false;
            }

            double lv = evalNum(l, st);
            double rv = evalNum(r, st);
            if (n.sval == "==") return lv == rv;
            if (n.sval == "!=") return lv != rv;
            if (n.sval == ">") return lv > rv;
            if (n.sval == "<") return lv < rv;
            if (n.sval == ">=") return lv >= rv;
            if (n.sval == "<=") return lv <= rv;
            return false;
        }
        case CondNode::NUM:
            return n.nval != 0;
        case CondNode::STR:
        default:
            return false;
    }
}

double RuleEngine::evalNum(const CondNode& n, const GameState& st) const {
    switch (n.type) {
        case CondNode::NUM: return n.nval;
        case CondNode::BOOL_LIT: return n.bval ? 1.0 : 0.0;
        case CondNode::VAR: return getVar(n.sval, st);
        default: return evalCond(n, st) ? 1.0 : 0.0;
    }
}

double RuleEngine::getVar(const std::string& name, const GameState& st) const {
    if (name == "in_check") return st.inCheck ? 1.0 : 0.0;
    if (name == "has_legal_moves") return st.hasLegalMoves ? 1.0 : 0.0;
    if (name == "no_legal_moves") return st.hasLegalMoves ? 0.0 : 1.0;
    if (name == "no_capture_count") return (double)st.noCaptureCount;
    if (name == "move_count") return (double)st.moveCount;
    if (name == "current_side") return (double)st.currentSide;
    if (name == "last_captured") return st.lastCaptured ? 1.0 : 0.0;
    if (name == "perpetual_check_count") return (double)st.perpetualCheckCount;
    if (name == "position_repeat_count") return (double)st.positionRepeatCount;
    return 0.0;
}

std::vector<RuleEngine::Tok> RuleEngine::tokenize(const std::string& s) const {
    std::vector<Tok> t;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        if (std::isspace(c)) { i++; continue; }

        if (c == '(') { t.push_back({Tok::LP, "("}); i++; continue; }
        if (c == ')') { t.push_back({Tok::RP, ")"}); i++; continue; }

        if (c == '"') {
            size_t j = i + 1;
            while (j < s.size() && s[j] != '"') j++;
            std::string text = (j > i + 1) ? s.substr(i + 1, j - i - 1) : "";
            t.push_back({Tok::STR, text});
            i = (j < s.size()) ? (j + 1) : s.size();
            continue;
        }

        if (std::isdigit(c)) {
            size_t j = i;
            while (j < s.size() && (std::isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
            std::string n = s.substr(i, j - i);
            t.push_back({Tok::NUM, n, std::stod(n)});
            i = j;
            continue;
        }

        if (std::isalpha(c) || c == '_') {
            size_t j = i;
            while (j < s.size()) {
                unsigned char d = (unsigned char)s[j];
                if (!std::isalnum(d) && d != '_') break;
                j++;
            }
            t.push_back({Tok::ID, s.substr(i, j - i)});
            i = j;
            continue;
        }

        if (c == '>' || c == '<' || c == '=' || c == '!') {
            if (i + 1 < s.size() && s[i + 1] == '=') {
                t.push_back({Tok::OP, s.substr(i, 2)});
                i += 2;
            } else {
                t.push_back({Tok::OP, s.substr(i, 1)});
                i += 1;
            }
            continue;
        }

        i++;
    }
    t.push_back({Tok::END, ""});
    return t;
}

CondNode RuleEngine::pOr(const std::vector<Tok>& t, size_t& p) const {
    CondNode left = pAnd(t, p);
    while (p < t.size() && t[p].type == Tok::ID && t[p].text == "or") {
        p++;
        CondNode right = pAnd(t, p);
        CondNode n;
        n.type = CondNode::OR;
        n.children.push_back(left);
        n.children.push_back(right);
        left = n;
    }
    return left;
}

CondNode RuleEngine::pAnd(const std::vector<Tok>& t, size_t& p) const {
    CondNode left = pNot(t, p);
    while (p < t.size() && t[p].type == Tok::ID && t[p].text == "and") {
        p++;
        CondNode right = pNot(t, p);
        CondNode n;
        n.type = CondNode::AND;
        n.children.push_back(left);
        n.children.push_back(right);
        left = n;
    }
    return left;
}

CondNode RuleEngine::pNot(const std::vector<Tok>& t, size_t& p) const {
    if (p < t.size() && t[p].type == Tok::ID && t[p].text == "not") {
        p++;
        CondNode n;
        n.type = CondNode::NOT_OP;
        n.children.push_back(pNot(t, p));
        return n;
    }
    return pCmp(t, p);
}

CondNode RuleEngine::pCmp(const std::vector<Tok>& t, size_t& p) const {
    CondNode left = pPrimary(t, p);
    if (p < t.size() && t[p].type == Tok::OP && isCmpOp(t[p].text)) {
        std::string op = t[p].text;
        p++;
        CondNode right = pPrimary(t, p);
        CondNode n;
        n.type = CondNode::CMP;
        n.sval = op;
        n.children.push_back(left);
        n.children.push_back(right);
        return n;
    }
    return left;
}

CondNode RuleEngine::pPrimary(const std::vector<Tok>& t, size_t& p) const {
    CondNode n;
    n.type = CondNode::BOOL_LIT;
    n.bval = false;

    if (p >= t.size()) return n;

    if (t[p].type == Tok::LP) {
        p++;
        CondNode e = pOr(t, p);
        if (p < t.size() && t[p].type == Tok::RP) p++;
        return e;
    }

    if (t[p].type == Tok::NUM) {
        n.type = CondNode::NUM;
        n.nval = t[p].num;
        p++;
        return n;
    }

    if (t[p].type == Tok::STR) {
        n.type = CondNode::STR;
        n.sval = t[p].text;
        p++;
        return n;
    }

    if (t[p].type == Tok::ID) {
        if (t[p].text == "true" || t[p].text == "false") {
            n.type = CondNode::BOOL_LIT;
            n.bval = (t[p].text == "true");
            p++;
            return n;
        }
        n.type = CondNode::VAR;
        n.sval = t[p].text;
        p++;
        return n;
    }

    p++;
    return n;
}
