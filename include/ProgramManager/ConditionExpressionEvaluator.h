// ConditionExpressionEvaluator.h
// Parses and evaluates conditional expressions against SequenceContext values
#pragma once

#include <string>
#include <vector>
#include <memory>

class SequenceContext;

/**
 * @brief Token types for expression parsing
 */
enum class ConditionTokenType {
    IDENTIFIER,     // Variable name (e.g., "current", "status")
    NUMBER,         // Numeric literal (e.g., 5, 10.5, -3.14)
    STRING,         // String literal (e.g., "pass", "fail")
    OPERATOR,       // Comparison operator (>, <, >=, <=, ==, !=)
    LOGICAL_AND,    // AND keyword
    LOGICAL_OR,     // OR keyword
    LPAREN,         // (
    RPAREN,         // )
    END_OF_INPUT,   // End of expression
    INVALID         // Invalid token
};

/**
 * @brief Token structure for lexer output
 */
struct ConditionToken {
    ConditionTokenType type;
    std::string value;          // Original text
    double numericValue = 0.0;  // Parsed numeric value (if NUMBER)

    ConditionToken() : type(ConditionTokenType::INVALID) {}
    ConditionToken(ConditionTokenType t, const std::string& v = "")
        : type(t), value(v), numericValue(0.0) {}
    ConditionToken(ConditionTokenType t, const std::string& v, double n)
        : type(t), value(v), numericValue(n) {}
};

/**
 * @brief Evaluates conditional expressions against SequenceContext
 *
 * Supports:
 * - Comparison operators: >, <, >=, <=, ==, !=
 * - Logical operators: AND, OR
 * - Parentheses for grouping
 * - Numeric and string values
 * - Context variable lookup
 *
 * Expression Grammar:
 *   expression  := or_expr
 *   or_expr     := and_expr ( "OR" and_expr )*
 *   and_expr    := comparison ( "AND" comparison )*
 *   comparison  := value OPERATOR value
 *   value       := NUMBER | STRING | IDENTIFIER | "(" expression ")"
 *
 * Examples:
 *   "current > 5"
 *   "current > 5 AND voltage < 10"
 *   "status == \"pass\" OR override == true"
 *   "(current > 5 AND voltage < 10) OR force_run == true"
 */
class ConditionExpressionEvaluator
{
public:
    ConditionExpressionEvaluator();
    ~ConditionExpressionEvaluator();

    /**
     * @brief Evaluate an expression against a SequenceContext
     * @param expression The condition expression string
     * @param context The SequenceContext containing variable values
     * @param errorMessage Output error message if evaluation fails
     * @return Result of evaluation, or true if expression is empty
     */
    bool Evaluate(const std::string& expression,
                  SequenceContext* context,
                  std::string& errorMessage) const;

    /**
     * @brief Validate expression syntax without evaluating
     * @param expression The condition expression string
     * @param errorMessage Output error message if invalid
     * @return True if syntax is valid
     */
    bool Validate(const std::string& expression,
                  std::string& errorMessage) const;

    /**
     * @brief Get list of variable names referenced in expression
     * @param expression The condition expression string
     * @return Vector of variable names (identifiers)
     */
    std::vector<std::string> GetReferencedVariables(const std::string& expression) const;

private:
    // Lexer - tokenize the expression
    std::vector<ConditionToken> Tokenize(const std::string& expression,
                                         std::string& errorMessage) const;

    // Parser - recursive descent
    // Returns evaluation result, sets errorMessage on failure
    bool ParseExpression(const std::vector<ConditionToken>& tokens,
                         size_t& pos,
                         SequenceContext* context,
                         std::string& errorMessage) const;

    bool ParseOrExpression(const std::vector<ConditionToken>& tokens,
                           size_t& pos,
                           SequenceContext* context,
                           std::string& errorMessage) const;

    bool ParseAndExpression(const std::vector<ConditionToken>& tokens,
                            size_t& pos,
                            SequenceContext* context,
                            std::string& errorMessage) const;

    bool ParseComparison(const std::vector<ConditionToken>& tokens,
                         size_t& pos,
                         SequenceContext* context,
                         std::string& errorMessage) const;

    // Value resolution from token and context
    double GetNumericValue(const ConditionToken& token,
                           SequenceContext* context,
                           bool& isNumeric) const;

    std::string GetStringValue(const ConditionToken& token,
                               SequenceContext* context) const;

    // Comparison execution
    bool CompareNumeric(double left, const std::string& op, double right) const;
    bool CompareString(const std::string& left, const std::string& op,
                       const std::string& right) const;

    // Helpers
    bool IsComparisonOperator(const std::string& s) const;
    void SkipWhitespace(const std::string& expr, size_t& pos) const;
};
