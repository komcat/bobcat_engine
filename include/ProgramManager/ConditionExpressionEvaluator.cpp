// ConditionExpressionEvaluator.cpp
// Implementation of conditional expression parser and evaluator

#include "ConditionExpressionEvaluator.h"
#include "../sequence/SequenceContext.h"
#include <iostream>
#include <cctype>
#include <algorithm>
#include <cmath>

ConditionExpressionEvaluator::ConditionExpressionEvaluator()
{
}

ConditionExpressionEvaluator::~ConditionExpressionEvaluator()
{
}

// ============================================================================
// PUBLIC METHODS
// ============================================================================

bool ConditionExpressionEvaluator::Evaluate(const std::string& expression,
                                             SequenceContext* context,
                                             std::string& errorMessage) const
{
    // Empty expression is always true (no condition = always proceed)
    std::string trimmed = expression;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    if (trimmed.empty()) {
        return true;
    }

    // Tokenize
    std::vector<ConditionToken> tokens = Tokenize(expression, errorMessage);
    if (tokens.empty() || !errorMessage.empty()) {
        return false;
    }

    // Parse and evaluate
    size_t pos = 0;
    bool result = ParseExpression(tokens, pos, context, errorMessage);

    // Check that we consumed all tokens
    if (errorMessage.empty() && pos < tokens.size() &&
        tokens[pos].type != ConditionTokenType::END_OF_INPUT) {
        errorMessage = "Unexpected token: " + tokens[pos].value;
        return false;
    }

    return result;
}

bool ConditionExpressionEvaluator::Validate(const std::string& expression,
                                             std::string& errorMessage) const
{
    // Empty expression is valid
    std::string trimmed = expression;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r"));
    trimmed.erase(trimmed.find_last_not_of(" \t\n\r") + 1);

    if (trimmed.empty()) {
        return true;
    }

    // Tokenize (validation mode - no context needed)
    std::vector<ConditionToken> tokens = Tokenize(expression, errorMessage);
    if (tokens.empty() || !errorMessage.empty()) {
        return false;
    }

    // Parse without evaluating (pass nullptr for context)
    size_t pos = 0;
    ParseExpression(tokens, pos, nullptr, errorMessage);

    // Check for unconsumed tokens
    if (errorMessage.empty() && pos < tokens.size() &&
        tokens[pos].type != ConditionTokenType::END_OF_INPUT) {
        errorMessage = "Unexpected token: " + tokens[pos].value;
        return false;
    }

    return errorMessage.empty();
}

std::vector<std::string> ConditionExpressionEvaluator::GetReferencedVariables(
    const std::string& expression) const
{
    std::vector<std::string> variables;
    std::string errorMsg;
    std::vector<ConditionToken> tokens = Tokenize(expression, errorMsg);

    for (const auto& token : tokens) {
        if (token.type == ConditionTokenType::IDENTIFIER) {
            // Exclude keywords
            std::string upper = token.value;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
            if (upper != "AND" && upper != "OR" && upper != "TRUE" && upper != "FALSE") {
                // Check if already in list
                if (std::find(variables.begin(), variables.end(), token.value) == variables.end()) {
                    variables.push_back(token.value);
                }
            }
        }
    }

    return variables;
}

// ============================================================================
// LEXER
// ============================================================================

std::vector<ConditionToken> ConditionExpressionEvaluator::Tokenize(
    const std::string& expression,
    std::string& errorMessage) const
{
    std::vector<ConditionToken> tokens;
    size_t pos = 0;

    while (pos < expression.size()) {
        SkipWhitespace(expression, pos);
        if (pos >= expression.size()) break;

        char c = expression[pos];

        // Parentheses
        if (c == '(') {
            tokens.emplace_back(ConditionTokenType::LPAREN, "(");
            pos++;
            continue;
        }
        if (c == ')') {
            tokens.emplace_back(ConditionTokenType::RPAREN, ")");
            pos++;
            continue;
        }

        // Comparison operators: >, <, >=, <=, ==, !=
        if (c == '>' || c == '<' || c == '=' || c == '!') {
            std::string op(1, c);
            if (pos + 1 < expression.size() && expression[pos + 1] == '=') {
                op += '=';
                pos++;
            } else if (c == '=' || c == '!') {
                // Single = or ! is invalid without =
                if (op.size() == 1) {
                    if (c == '=') {
                        // Check for ==
                        if (pos + 1 < expression.size() && expression[pos + 1] == '=') {
                            op += '=';
                            pos++;
                        } else {
                            errorMessage = "Invalid operator: single '=' (use '==' for equality)";
                            return {};
                        }
                    } else {
                        // Single !
                        if (pos + 1 < expression.size() && expression[pos + 1] == '=') {
                            op += '=';
                            pos++;
                        } else {
                            errorMessage = "Invalid operator: single '!' (use '!=' for not equal)";
                            return {};
                        }
                    }
                }
            }
            tokens.emplace_back(ConditionTokenType::OPERATOR, op);
            pos++;
            continue;
        }

        // String literal (double quotes)
        if (c == '"') {
            pos++; // Skip opening quote
            std::string str;
            while (pos < expression.size() && expression[pos] != '"') {
                if (expression[pos] == '\\' && pos + 1 < expression.size()) {
                    // Handle escape sequences
                    pos++;
                    char escaped = expression[pos];
                    if (escaped == 'n') str += '\n';
                    else if (escaped == 't') str += '\t';
                    else if (escaped == '"') str += '"';
                    else if (escaped == '\\') str += '\\';
                    else str += escaped;
                } else {
                    str += expression[pos];
                }
                pos++;
            }
            if (pos >= expression.size()) {
                errorMessage = "Unterminated string literal";
                return {};
            }
            pos++; // Skip closing quote
            tokens.emplace_back(ConditionTokenType::STRING, str);
            continue;
        }

        // Number (including negative)
        if (std::isdigit(c) || (c == '-' && pos + 1 < expression.size() &&
                                 std::isdigit(expression[pos + 1]))) {
            std::string numStr;
            if (c == '-') {
                numStr += c;
                pos++;
            }
            while (pos < expression.size() &&
                   (std::isdigit(expression[pos]) || expression[pos] == '.')) {
                numStr += expression[pos];
                pos++;
            }
            try {
                double value = std::stod(numStr);
                tokens.emplace_back(ConditionTokenType::NUMBER, numStr, value);
            } catch (...) {
                errorMessage = "Invalid number: " + numStr;
                return {};
            }
            continue;
        }

        // Identifier or keyword (AND, OR, true, false)
        if (std::isalpha(c) || c == '_') {
            std::string ident;
            while (pos < expression.size() &&
                   (std::isalnum(expression[pos]) || expression[pos] == '_')) {
                ident += expression[pos];
                pos++;
            }

            // Check for keywords
            std::string upper = ident;
            std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

            if (upper == "AND") {
                tokens.emplace_back(ConditionTokenType::LOGICAL_AND, ident);
            } else if (upper == "OR") {
                tokens.emplace_back(ConditionTokenType::LOGICAL_OR, ident);
            } else if (upper == "TRUE") {
                tokens.emplace_back(ConditionTokenType::NUMBER, ident, 1.0);
            } else if (upper == "FALSE") {
                tokens.emplace_back(ConditionTokenType::NUMBER, ident, 0.0);
            } else {
                tokens.emplace_back(ConditionTokenType::IDENTIFIER, ident);
            }
            continue;
        }

        // Unknown character
        errorMessage = std::string("Unexpected character: '") + c + "'";
        return {};
    }

    tokens.emplace_back(ConditionTokenType::END_OF_INPUT, "");
    return tokens;
}

void ConditionExpressionEvaluator::SkipWhitespace(const std::string& expr,
                                                   size_t& pos) const
{
    while (pos < expr.size() && std::isspace(expr[pos])) {
        pos++;
    }
}

// ============================================================================
// PARSER (Recursive Descent)
// ============================================================================

bool ConditionExpressionEvaluator::ParseExpression(
    const std::vector<ConditionToken>& tokens,
    size_t& pos,
    SequenceContext* context,
    std::string& errorMessage) const
{
    return ParseOrExpression(tokens, pos, context, errorMessage);
}

bool ConditionExpressionEvaluator::ParseOrExpression(
    const std::vector<ConditionToken>& tokens,
    size_t& pos,
    SequenceContext* context,
    std::string& errorMessage) const
{
    bool result = ParseAndExpression(tokens, pos, context, errorMessage);
    if (!errorMessage.empty()) return false;

    while (pos < tokens.size() && tokens[pos].type == ConditionTokenType::LOGICAL_OR) {
        pos++; // Consume OR

        bool right = ParseAndExpression(tokens, pos, context, errorMessage);
        if (!errorMessage.empty()) return false;

        result = result || right;
    }

    return result;
}

bool ConditionExpressionEvaluator::ParseAndExpression(
    const std::vector<ConditionToken>& tokens,
    size_t& pos,
    SequenceContext* context,
    std::string& errorMessage) const
{
    bool result = ParseComparison(tokens, pos, context, errorMessage);
    if (!errorMessage.empty()) return false;

    while (pos < tokens.size() && tokens[pos].type == ConditionTokenType::LOGICAL_AND) {
        pos++; // Consume AND

        bool right = ParseComparison(tokens, pos, context, errorMessage);
        if (!errorMessage.empty()) return false;

        result = result && right;
    }

    return result;
}

bool ConditionExpressionEvaluator::ParseComparison(
    const std::vector<ConditionToken>& tokens,
    size_t& pos,
    SequenceContext* context,
    std::string& errorMessage) const
{
    if (pos >= tokens.size()) {
        errorMessage = "Unexpected end of expression";
        return false;
    }

    // Handle parentheses
    if (tokens[pos].type == ConditionTokenType::LPAREN) {
        pos++; // Consume (
        bool result = ParseExpression(tokens, pos, context, errorMessage);
        if (!errorMessage.empty()) return false;

        if (pos >= tokens.size() || tokens[pos].type != ConditionTokenType::RPAREN) {
            errorMessage = "Missing closing parenthesis";
            return false;
        }
        pos++; // Consume )
        return result;
    }

    // Get left operand
    const ConditionToken& left = tokens[pos];
    if (left.type != ConditionTokenType::IDENTIFIER &&
        left.type != ConditionTokenType::NUMBER &&
        left.type != ConditionTokenType::STRING) {
        errorMessage = "Expected value, got: " + left.value;
        return false;
    }
    pos++;

    // Get operator
    if (pos >= tokens.size() || tokens[pos].type != ConditionTokenType::OPERATOR) {
        errorMessage = "Expected comparison operator (>, <, >=, <=, ==, !=)";
        return false;
    }
    std::string op = tokens[pos].value;
    pos++;

    // Get right operand
    if (pos >= tokens.size()) {
        errorMessage = "Expected value after operator";
        return false;
    }
    const ConditionToken& right = tokens[pos];
    if (right.type != ConditionTokenType::IDENTIFIER &&
        right.type != ConditionTokenType::NUMBER &&
        right.type != ConditionTokenType::STRING) {
        errorMessage = "Expected value, got: " + right.value;
        return false;
    }
    pos++;

    // If context is nullptr (validation mode), just return true
    if (context == nullptr) {
        return true;
    }

    // Resolve values and compare
    bool leftIsNumeric = false, rightIsNumeric = false;
    double leftNum = GetNumericValue(left, context, leftIsNumeric);
    double rightNum = GetNumericValue(right, context, rightIsNumeric);

    // If both can be numeric, do numeric comparison
    if (leftIsNumeric && rightIsNumeric) {
        return CompareNumeric(leftNum, op, rightNum);
    }

    // Otherwise do string comparison
    std::string leftStr = GetStringValue(left, context);
    std::string rightStr = GetStringValue(right, context);
    return CompareString(leftStr, op, rightStr);
}

// ============================================================================
// VALUE RESOLUTION
// ============================================================================

double ConditionExpressionEvaluator::GetNumericValue(
    const ConditionToken& token,
    SequenceContext* context,
    bool& isNumeric) const
{
    isNumeric = false;

    if (token.type == ConditionTokenType::NUMBER) {
        isNumeric = true;
        return token.numericValue;
    }

    if (token.type == ConditionTokenType::STRING) {
        // Try to parse string as number
        try {
            double val = std::stod(token.value);
            isNumeric = true;
            return val;
        } catch (...) {
            return 0.0;
        }
    }

    if (token.type == ConditionTokenType::IDENTIFIER && context != nullptr) {
        // Look up in context
        if (context->HasValue(token.value)) {
            auto valType = context->GetValueType(token.value);
            if (valType == SequenceContext::ValueType::NUMERIC ||
                valType == SequenceContext::ValueType::BOOLEAN) {
                isNumeric = true;
                return context->GetNumeric(token.value, 0.0);
            }
            // Try to parse string value as number
            std::string strVal = context->GetString(token.value, "");
            try {
                double val = std::stod(strVal);
                isNumeric = true;
                return val;
            } catch (...) {
                // Not a number
            }
        }
    }

    return 0.0;
}

std::string ConditionExpressionEvaluator::GetStringValue(
    const ConditionToken& token,
    SequenceContext* context) const
{
    if (token.type == ConditionTokenType::STRING) {
        return token.value;
    }

    if (token.type == ConditionTokenType::NUMBER) {
        // Convert number to string
        if (std::floor(token.numericValue) == token.numericValue) {
            return std::to_string(static_cast<long long>(token.numericValue));
        }
        return std::to_string(token.numericValue);
    }

    if (token.type == ConditionTokenType::IDENTIFIER && context != nullptr) {
        // Look up in context
        return context->GetString(token.value, token.value);
    }

    return token.value;
}

// ============================================================================
// COMPARISON HELPERS
// ============================================================================

bool ConditionExpressionEvaluator::CompareNumeric(double left,
                                                   const std::string& op,
                                                   double right) const
{
    const double epsilon = 1e-9;

    if (op == ">") return left > right;
    if (op == "<") return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == "==") return std::fabs(left - right) < epsilon;
    if (op == "!=") return std::fabs(left - right) >= epsilon;

    return false;
}

bool ConditionExpressionEvaluator::CompareString(const std::string& left,
                                                  const std::string& op,
                                                  const std::string& right) const
{
    if (op == "==") return left == right;
    if (op == "!=") return left != right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;

    return false;
}

bool ConditionExpressionEvaluator::IsComparisonOperator(const std::string& s) const
{
    return s == ">" || s == "<" || s == ">=" || s == "<=" || s == "==" || s == "!=";
}
