#pragma once
#include <string>
#include <vector>

// 规则脚本解释器
// 规则文件格式:
//   rule "规则名称"
//   when <条件表达式>
//   action <动作>
//
// 条件表达式中可用的变量:
//   in_check                — 当前方是否被将军 (bool)
//   has_legal_moves         — 当前方是否有合法走法 (bool)
//   no_capture_count        — 连续无吃子半步数 (int)
//   move_count              — 总步数/半步 (int)
//   current_side            — 当前走棋方 ("red" / "black")
//   last_captured           — 上一步是否吃子 (bool)
//   perpetual_check_count   — 连续将军循环中局面重复次数
//   position_repeat_count   — 当前局面总重复出现次数（含当前）
//
// 支持的运算符:
//   比较: >=  <=  ==  !=  >  <
//   逻辑: and  or  not
//   括号: ( )
//
// 支持的动作:
//   checkmate  — 判当前方被将死 (对方胜)
//   draw       — 判和棋
//   lose       — 判当前方负
//   win "消息"  — 判当前方胜 (用于长将/循环违例等)
//   warn "消息" — 显示提示 (游戏继续)

enum class RuleAction {
    NONE,
    CHECKMATE,
    DRAW,
    LOSE,
    WIN,
    WARN
};

struct RuleResult {
    RuleAction action = RuleAction::NONE;
    std::string message;
    std::string ruleName;
};

struct GameState {
    bool inCheck = false;
    bool hasLegalMoves = true;
    int noCaptureCount = 0;
    int moveCount = 0;
    int currentSide = 0;              // 0=red, 1=black
    bool lastCaptured = false;
    int perpetualCheckCount = 0;      // 连续将军循环中局面重复次数
    int positionRepeatCount = 0;      // 当前局面总重复次数（含当前）
};

struct ParsedRule {
    std::string name;
    std::string conditionStr;
    std::string actionStr;
};

// 条件 AST
struct CondNode {
    enum Type { VAR, NUM, STR, CMP, AND, OR, NOT_OP, BOOL_LIT };
    Type type;
    std::string sval;
    double nval = 0;
    bool bval = false;
    std::vector<CondNode> children;
};

class RuleEngine {
public:
    void loadFromFile(const std::string& path);
    void reloadFromFile();

    RuleResult evaluate(const GameState& state) const;

    const std::vector<ParsedRule>& rules() const { return m_rules; }
    const std::string& filePath() const { return m_filePath; }

private:
    std::vector<ParsedRule> parseFile(const std::string& content);
    CondNode parseCondition(const std::string& expr) const;
    RuleAction parseAction(const std::string& s, std::string& msg) const;

    bool evalCond(const CondNode& n, const GameState& st) const;
    double evalNum(const CondNode& n, const GameState& st) const;
    double getVar(const std::string& name, const GameState& st) const;

    // tokenizer + recursive descent parser
    struct Tok {
        enum Type { NUM, STR, ID, OP, LP, RP, END };
        Type type; std::string text; double num = 0;
    };
    std::vector<Tok> tokenize(const std::string& s) const;
    CondNode pOr(const std::vector<Tok>& t, size_t& p) const;
    CondNode pAnd(const std::vector<Tok>& t, size_t& p) const;
    CondNode pNot(const std::vector<Tok>& t, size_t& p) const;
    CondNode pCmp(const std::vector<Tok>& t, size_t& p) const;
    CondNode pPrimary(const std::vector<Tok>& t, size_t& p) const;

    std::vector<ParsedRule> m_rules;
    std::string m_filePath;
};
