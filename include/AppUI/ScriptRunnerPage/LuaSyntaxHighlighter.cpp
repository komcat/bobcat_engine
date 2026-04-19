// LuaSyntaxHighlighter.cpp - Lua syntax highlighting rules
#include "LuaSyntaxHighlighter.h"

LuaSyntaxHighlighter::LuaSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent)
{
    // Keywords (purple)
    m_keywordFormat.setForeground(QColor("#c586c0"));
    m_keywordFormat.setFontWeight(QFont::Bold);
    const QStringList keywords = {
        "and", "break", "do", "else", "elseif", "end",
        "false", "for", "function", "goto", "if", "in",
        "local", "nil", "not", "or", "repeat", "return",
        "then", "true", "until", "while"
    };
    for (const QString& kw : keywords) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\\b" + kw + "\\b");
        rule.format = m_keywordFormat;
        m_rules.append(rule);
    }

    // Bobcat modules (teal)
    m_moduleFormat.setForeground(QColor("#4ec9b0"));
    const QStringList modules = {
        "motion", "io", "pneumatic", "camera", "vision",
        "tec", "psu", "smu", "lighting", "ctx", "log",
        "system", "datastore", "math", "string", "table"
    };
    for (const QString& mod : modules) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\\b" + mod + "\\b");
        rule.format = m_moduleFormat;
        m_rules.append(rule);
    }

    // Built-in functions (yellow)
    m_builtinFormat.setForeground(QColor("#dcdcaa"));
    const QStringList builtins = {
        "print", "tostring", "tonumber", "type", "pairs",
        "ipairs", "error", "pcall", "xpcall", "select",
        "unpack", "assert", "rawget", "rawset", "setmetatable",
        "getmetatable", "next", "_dispatch"
    };
    for (const QString& fn : builtins) {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\\b" + fn + "\\b");
        rule.format = m_builtinFormat;
        m_rules.append(rule);
    }

    // Numbers (light green)
    m_numberFormat.setForeground(QColor("#b5cea8"));
    {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\\b\\d+\\.?\\d*([eE][+-]?\\d+)?\\b");
        rule.format = m_numberFormat;
        m_rules.append(rule);
    }
    {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\\b0x[0-9a-fA-F]+\\b");
        rule.format = m_numberFormat;
        m_rules.append(rule);
    }

    // Strings (orange) — single and double quoted
    m_stringFormat.setForeground(QColor("#ce9178"));
    {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("\"[^\"\\\\]*(\\\\.[^\"\\\\]*)*\"");
        rule.format = m_stringFormat;
        m_rules.append(rule);
    }
    {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("'[^'\\\\]*(\\\\.[^'\\\\]*)*'");
        rule.format = m_stringFormat;
        m_rules.append(rule);
    }

    // Single-line comments (green)
    m_commentFormat.setForeground(QColor("#6a9955"));
    m_commentFormat.setFontItalic(true);
    {
        HighlightingRule rule;
        rule.pattern = QRegularExpression("--(?!\\[\\[)[^\n]*");
        rule.format = m_commentFormat;
        m_rules.append(rule);
    }
}

void LuaSyntaxHighlighter::highlightBlock(const QString& text)
{
    // Apply single-line rules first
    for (const HighlightingRule& rule : m_rules) {
        QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }

    // Multi-line comment/string handling: --[[ ... ]] and [[ ... ]]
    setCurrentBlockState(Normal);

    int startIndex = 0;
    int prevState = previousBlockState();

    // Handle multi-line comments --[[ ... ]]
    if (prevState != InMultiLineComment) {
        startIndex = text.indexOf("--[[");
        if (startIndex >= 0) {
            startIndex += 0; // include the --[[ prefix
        }
    } else {
        startIndex = 0;
    }

    while (startIndex >= 0) {
        int endIndex = -1;
        int commentLength = 0;

        if (prevState == InMultiLineComment) {
            endIndex = text.indexOf("]]", startIndex);
        } else {
            endIndex = text.indexOf("]]", startIndex + 4);
        }

        if (endIndex == -1) {
            setCurrentBlockState(InMultiLineComment);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + 2;
        }

        setFormat(startIndex, commentLength, m_commentFormat);

        if (endIndex >= 0) {
            startIndex = text.indexOf("--[[", startIndex + commentLength);
            prevState = Normal;
        } else {
            break;
        }
    }

    // Handle multi-line strings [[ ... ]] (not preceded by --)
    if (currentBlockState() != InMultiLineComment) {
        startIndex = 0;

        if (prevState == InMultiLineString) {
            startIndex = 0;
        } else {
            // Find [[ not preceded by --
            int pos = 0;
            startIndex = -1;
            while (pos < text.length() - 1) {
                int idx = text.indexOf("[[", pos);
                if (idx < 0) break;
                if (idx < 2 || text.mid(idx - 2, 2) != "--") {
                    startIndex = idx;
                    break;
                }
                pos = idx + 2;
            }
        }

        while (startIndex >= 0) {
            int endIndex = -1;
            int strLength = 0;

            if (prevState == InMultiLineString) {
                endIndex = text.indexOf("]]", startIndex);
            } else {
                endIndex = text.indexOf("]]", startIndex + 2);
            }

            if (endIndex == -1) {
                setCurrentBlockState(InMultiLineString);
                strLength = text.length() - startIndex;
            } else {
                strLength = endIndex - startIndex + 2;
            }

            setFormat(startIndex, strLength, m_stringFormat);

            if (endIndex >= 0) {
                int pos = startIndex + strLength;
                startIndex = -1;
                while (pos < text.length() - 1) {
                    int idx = text.indexOf("[[", pos);
                    if (idx < 0) break;
                    if (idx < 2 || text.mid(idx - 2, 2) != "--") {
                        startIndex = idx;
                        break;
                    }
                    pos = idx + 2;
                }
                prevState = Normal;
            } else {
                break;
            }
        }
    }
}
