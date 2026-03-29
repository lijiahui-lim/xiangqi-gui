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
#   check_capture_cycle_count   — 一将一捉无根子循环次数
#
# 动作:
#   checkmate  — 被将死/困毙
#   draw       — 和棋
#   lose       — 当前方负
#   win "消息" — 当前方胜
#   warn "消息" — 显示提示

rule "循环长将警告"
when in_check and perpetual_check_count >= 2
action warn "循环长将! 请变招!"

rule "一将一捉判负"
when check_capture_cycle_count >= 3
action win "一将一捉无根子违例!"

rule "一将一捉警告"
when check_capture_cycle_count >= 2
action warn "一将一捉! 请变招!"

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
        // 兼容旧格式(开关表)或非法格式：自动重置成脚本模板
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
    }
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
    if (name == "check_capture_cycle_count") return (double)st.checkCaptureCycleCount;
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

// ========== 一将一捉无根子检测 ==========

// 判断 (r,c) 处的棋子是否有根（被友方棋子保护）
// 方法: 将该位置替换为敌方棋子，看友方能否吃到该格
static bool isPieceProtected(Board& board, int r, int c) {
    Piece target = board.get(r, c);
    if (target.empty()) return false;
    Side friendly = target.side;
    Side enemy = (friendly == Side::RED) ? Side::BLACK : Side::RED;

    // 临时替换为敌方兵/卒，这样友方的"吃子走法"就能覆盖此格
    Piece saved = target;
    board.set(r, c, {PieceType::PAWN, enemy});

    bool defended = false;
    for (int fr = 0; fr < Board::ROWS && !defended; fr++) {
        for (int fc = 0; fc < Board::COLS && !defended; fc++) {
            if (fr == r && fc == c) continue;
            Piece p = board.get(fr, fc);
            if (p.side != friendly) continue;
            auto moves = board.getLegalMoves(fr, fc);
            for (const auto& m : moves) {
                if (m.toRow == r && m.toCol == c) {
                    defended = true;
                    break;
                }
            }
        }
    }

    board.set(r, c, saved);
    return defended;
}

// 检测局面中进攻方（刚走完棋的一方）是否在"捉"对方的无根子
// board 的 currentSide 是受害方（即将走棋方）
static bool hasThreatenedUnprotectedPiece(Board& board) {
    Side victim = board.currentSide();
    Side attacker = (victim == Side::RED) ? Side::BLACK : Side::RED;

    for (int fr = 0; fr < Board::ROWS; fr++) {
        for (int fc = 0; fc < Board::COLS; fc++) {
            Piece p = board.get(fr, fc);
            if (p.side != attacker) continue;
            auto moves = board.getLegalMoves(fr, fc);
            for (const auto& m : moves) {
                Piece target = board.get(m.toRow, m.toCol);
                if (!target.empty() && target.side == victim &&
                    target.type != PieceType::KING) {
                    if (!isPieceProtected(board, m.toRow, m.toCol)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

int RuleEngine::detectCheckCaptureCycle(const std::vector<std::string>& posHistory) const {
    int n = (int)posHistory.size();
    if (n < 4) return 0;  // 至少需要 4 个半步才可能形成一个循环

    // 分析进攻方的最近走法:
    // posHistory[n-1], posHistory[n-3], posHistory[n-5] 是同一方（受害方）的局面
    // 在这些局面中，进攻方刚走完棋
    // 检查是否呈现: 将 + 捉 交替的模式

    int checkCount = 0;
    int captureCount = 0;
    int analyzedCount = 0;

    for (int k = 1; k <= 6 && (n - k) >= 0; k += 2) {
        int idx = n - k;
        Board b;
        b.fromFEN(posHistory[idx]);
        Side victim = b.currentSide();

        bool inCheck = b.isInCheck(victim);
        bool hasThreat = false;
        if (!inCheck) {
            hasThreat = hasThreatenedUnprotectedPiece(b);
        }

        if (inCheck) {
            checkCount++;
        } else if (hasThreat) {
            captureCount++;
        } else {
            break;  // 模式中断
        }
        analyzedCount++;
    }

    // 一将一捉: 需要同时存在"将"和"捉无根子"
    if (checkCount >= 1 && captureCount >= 1 && analyzedCount >= 2) {
        return analyzedCount;  // 返回循环中的进攻步数
    }

    return 0;
}
