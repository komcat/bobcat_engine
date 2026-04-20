// ScriptEditorWidget.h - Code editor with line numbers, breakpoints, debug, and autocomplete
#pragma once

#include <QPlainTextEdit>
#include <QCompleter>
#include <QStringListModel>
#include <QAbstractItemView>
#include <QScrollBar>
#include <QPainter>
#include <QTextBlock>
#include <QMouseEvent>
#include <QToolTip>
#include <set>
#include <map>

class ScriptEditorWidget : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit ScriptEditorWidget(QWidget* parent = nullptr)
        : QPlainTextEdit(parent)
    {
        m_lineNumberArea = new LineNumberArea(this);

        connect(this, &QPlainTextEdit::blockCountChanged, this, &ScriptEditorWidget::updateLineNumberAreaWidth);
        connect(this, &QPlainTextEdit::updateRequest, this, &ScriptEditorWidget::updateLineNumberArea);
        connect(this, &QPlainTextEdit::cursorPositionChanged, this, &ScriptEditorWidget::highlightCurrentLine);

        setFont(QFont("Consolas", 10));
        setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
        setLineWrapMode(QPlainTextEdit::NoWrap);

        updateLineNumberAreaWidth(0);
        highlightCurrentLine();
        setupCompleter();
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) { max /= 10; ++digits; }
        return 20 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

    void setDebugLine(int line)
    {
        m_debugLine = line;
        if (line > 0) {
            QTextBlock block = document()->findBlockByNumber(line - 1);
            if (block.isValid()) {
                QTextCursor cursor(block);
                setTextCursor(cursor);
                centerCursor();
            }
        }
        highlightCurrentLine();
        m_lineNumberArea->update();
    }

    void clearDebugLine()
    {
        m_debugLine = -1;
        highlightCurrentLine();
        m_lineNumberArea->update();
    }

    void toggleBreakpoint(int line)
    {
        if (m_breakpointLines.count(line))
            m_breakpointLines.erase(line);
        else
            m_breakpointLines.insert(line);
        m_lineNumberArea->update();
        emit breakpointToggled(line);
    }

    std::set<int> getBreakpoints() const { return m_breakpointLines; }
    void clearBreakpoints() { m_breakpointLines.clear(); m_lineNumberArea->update(); }

signals:
    void breakpointToggled(int line);

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        // If completer popup is visible, let it handle navigation keys
        if (m_completer && m_completer->popup()->isVisible()) {
            switch (event->key()) {
            case Qt::Key_Enter:
            case Qt::Key_Return:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                event->ignore();
                return;
            default:
                break;
            }
        }

        // Ctrl+Space: force show completions
        bool ctrlSpace = (event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space;

        if (event->key() == Qt::Key_Tab && !(m_completer && m_completer->popup()->isVisible())) {
            insertPlainText("    ");
            return;
        }

        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (m_completer && m_completer->popup()->isVisible()) {
                event->ignore();
                return;
            }
            QTextCursor cursor = textCursor();
            QString currentLine = cursor.block().text();
            QString indent;
            for (QChar ch : currentLine) {
                if (ch == ' ' || ch == '\t') indent += ch;
                else break;
            }
            QPlainTextEdit::keyPressEvent(event);
            insertPlainText(indent);
            return;
        }

        QPlainTextEdit::keyPressEvent(event);

        // Determine completion context
        if (ctrlSpace || event->key() == Qt::Key_Period ||
            (event->text().length() == 1 && (event->text()[0].isLetterOrNumber() || event->text()[0] == '_'))) {
            updateCompletions();
        } else {
            if (m_completer) m_completer->popup()->hide();
        }
    }

    void focusInEvent(QFocusEvent* event) override
    {
        if (m_completer) m_completer->setWidget(this);
        QPlainTextEdit::focusInEvent(event);
    }

private slots:
    void updateLineNumberAreaWidth(int /*newBlockCount*/)
    {
        setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
    }

    void highlightCurrentLine()
    {
        QList<QTextEdit::ExtraSelection> extraSelections;

        if (!isReadOnly()) {
            QTextEdit::ExtraSelection cursorSel;
            cursorSel.format.setBackground(QColor("#2a2d2e"));
            cursorSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            cursorSel.cursor = textCursor();
            cursorSel.cursor.clearSelection();
            extraSelections.append(cursorSel);
        }

        if (m_debugLine > 0) {
            QTextBlock block = document()->findBlockByNumber(m_debugLine - 1);
            if (block.isValid()) {
                QTextEdit::ExtraSelection debugSel;
                debugSel.format.setBackground(QColor("#804000"));
                debugSel.format.setProperty(QTextFormat::FullWidthSelection, true);
                debugSel.cursor = QTextCursor(block);
                debugSel.cursor.clearSelection();
                extraSelections.append(debugSel);
            }
        }

        setExtraSelections(extraSelections);
    }

    void updateLineNumberArea(const QRect& rect, int dy)
    {
        if (dy)
            m_lineNumberArea->scroll(0, dy);
        else
            m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());

        if (rect.contains(viewport()->rect()))
            updateLineNumberAreaWidth(0);
    }

    void insertCompletion(const QString& completion)
    {
        if (!m_completer) return;
        QTextCursor tc = textCursor();

        // If we completed after a dot, only insert the method part
        if (m_completingAfterDot) {
            int extra = completion.length() - m_completer->completionPrefix().length();
            tc.movePosition(QTextCursor::Left);
            tc.movePosition(QTextCursor::EndOfWord);
            tc.insertText(completion.right(extra));
        } else {
            int extra = completion.length() - m_completer->completionPrefix().length();
            tc.movePosition(QTextCursor::Left);
            tc.movePosition(QTextCursor::EndOfWord);
            tc.insertText(completion.right(extra));
        }
        setTextCursor(tc);
    }

private:
    void setupCompleter()
    {
        m_completer = new QCompleter(this);
        m_completer->setWidget(this);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setMaxVisibleItems(12);
        connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
                this, &ScriptEditorWidget::insertCompletion);

        m_globalCompletions = {
            // Modules
            "motion", "io", "pneumatic", "camera", "vision",
            "tec", "psu", "smu", "lighting", "ctx", "log",
            "system", "datastore", "math", "string", "table",
            "dut", "bobcat", "auxwindow", "cameraoffset",
            "coordalign", "relpos", "trainer", "beamserver",
            // Keywords
            "and", "break", "do", "else", "elseif", "end",
            "false", "for", "function", "goto", "if", "in",
            "local", "nil", "not", "or", "repeat", "return",
            "then", "true", "until", "while",
            // Built-ins
            "print", "tostring", "tonumber", "type", "pairs",
            "ipairs", "error", "pcall", "xpcall", "select",
            "_dispatch"
        };

        m_moduleCompletions["motion"] = {
            "moveToNamed", "moveAbs", "moveRel", "home",
            "getPosition", "setVelocity", "waitDone",
            "moveToGraph", "parallelMove", "parallelMoveRel",
            "emergencyStop", "stopAxis", "enableServo",
            "savePosition", "moveToContext", "moveToContextXY", "moveToContextPos",
            "updatePosition", "updatePositionAxis",
            "gridScan", "axisScan", "xcan",
            "registerDeviceLink", "compensateDeviceLink"
        };
        m_moduleCompletions["io"] = {
            "setOutput", "setOutputPin", "setOutputName", "clearOutputName",
            "getInput", "readInputs", "readOutputs",
            "waitInput", "pulse", "clearLatch"
        };
        m_moduleCompletions["pneumatic"] = {
            "extend", "retract", "retractAll", "stop",
            "getPosition", "getStatus",
            "waitExtended", "waitRetracted"
        };
        m_moduleCompletions["camera"] = {
            "capture", "captureToFile", "captureToContext", "captureToDUT",
            "captureAndConfirm", "checkPresent",
            "setExposure", "setGain", "setAutoExposure", "setAutoGain"
        };
        m_moduleCompletions["vision"] = {
            "runProfile", "autoFocus", "loadProfile",
            "enableCvInject", "disableCvInject"
        };
        m_moduleCompletions["tec"] = {
            "setTemp", "enable", "disable", "readTemp", "waitTemp",
            "setLaserCurrent", "readLaserCurrent", "laserOn", "laserOff",
            "getStatus", "connect", "disconnect"
        };
        m_moduleCompletions["psu"] = {
            "setVoltage", "setCurrent", "enable", "disable",
            "readVoltage", "readCurrent",
            "readVoltageMultiple", "readCurrentMultiple",
            "connect", "disconnect"
        };
        m_moduleCompletions["smu"] = {
            "readCurrent", "readVoltage", "setVoltage", "setCurrent",
            "enable", "disable", "setCompliance", "setMode",
            "startPolling", "stopPolling", "connect", "disconnect"
        };
        m_moduleCompletions["lighting"] = {
            "setIntensity", "turnOn", "turnOff", "allOff", "allOn", "getStatus"
        };
        m_moduleCompletions["dut"] = {
            "create", "load", "loadOrCreate", "close",
            "insertString", "insertDouble", "insertInteger",
            "insertFromContext", "deleteEntry"
        };
        m_moduleCompletions["bobcat"] = {
            "wait", "delay", "prompt", "askInput", "logMsg", "stopSequence"
        };
        m_moduleCompletions["auxwindow"] = {
            "setTarget", "clearTarget", "enableCrosshair"
        };
        m_moduleCompletions["cameraoffset"] = {
            "moveToHighMag", "moveToLowMag"
        };
        m_moduleCompletions["coordalign"] = {
            "transformPoint", "moveToLocalXY"
        };
        m_moduleCompletions["relpos"] = {
            "captureBaseline", "getDelta", "clearBaseline"
        };
        m_moduleCompletions["trainer"] = {
            "runAlignment", "runEdgeTransfer",
            "runPlanarAdjustment", "runZOffset", "runYawCorrection"
        };
        m_moduleCompletions["beamserver"] = {
            "connect", "disconnect", "detect", "fit", "reset", "save"
        };
        m_moduleCompletions["ctx"] = {
            "set", "setString", "get", "getNumeric", "getString",
            "getBoolean", "has", "clear", "keys"
        };
        m_moduleCompletions["log"] = { "info", "warn", "error", "debug" };
        m_moduleCompletions["system"] = { "wait" };
        m_moduleCompletions["datastore"] = { "set", "get", "readAndLog" };
        m_moduleCompletions["math"] = {
            "abs", "ceil", "floor", "sqrt", "sin", "cos", "tan",
            "atan", "exp", "log", "max", "min", "pi", "huge",
            "random", "randomseed"
        };
        m_moduleCompletions["string"] = {
            "format", "find", "sub", "len", "upper", "lower",
            "rep", "reverse", "byte", "char", "match", "gmatch", "gsub"
        };
        m_moduleCompletions["table"] = {
            "insert", "remove", "sort", "concat", "move", "unpack"
        };

        m_completionModel = new QStringListModel(this);
        m_completer->setModel(m_completionModel);
    }

    void updateCompletions()
    {
        QTextCursor tc = textCursor();
        tc.select(QTextCursor::LineUnderCursor);
        QString lineText = tc.selectedText();
        int cursorInLine = textCursor().positionInBlock();

        // Find the word being typed (walk backwards from cursor)
        QString prefix;
        QString moduleName;
        m_completingAfterDot = false;

        int i = cursorInLine - 1;
        // Collect current word
        while (i >= 0 && (lineText[i].isLetterOrNumber() || lineText[i] == '_')) {
            prefix.prepend(lineText[i]);
            --i;
        }

        // Check if there's a dot before the word
        if (i >= 0 && lineText[i] == '.') {
            m_completingAfterDot = true;
            --i;
            while (i >= 0 && (lineText[i].isLetterOrNumber() || lineText[i] == '_')) {
                moduleName.prepend(lineText[i]);
                --i;
            }
        }

        QStringList completions;
        if (m_completingAfterDot && m_moduleCompletions.count(moduleName)) {
            completions = m_moduleCompletions[moduleName];
        } else if (!m_completingAfterDot) {
            completions = m_globalCompletions;
        }

        if (completions.isEmpty() || prefix.isEmpty()) {
            m_completer->popup()->hide();
            return;
        }

        m_completionModel->setStringList(completions);
        m_completer->setCompletionPrefix(prefix);

        if (m_completer->completionCount() == 0) {
            m_completer->popup()->hide();
            return;
        }

        // Position popup at cursor
        QRect cr = cursorRect();
        cr.setWidth(m_completer->popup()->sizeHintForColumn(0)
                    + m_completer->popup()->verticalScrollBar()->sizeHint().width() + 20);
        m_completer->complete(cr);
    }

    int m_debugLine = -1;
    std::set<int> m_breakpointLines;
    bool m_completingAfterDot = false;

    QCompleter* m_completer = nullptr;
    QStringListModel* m_completionModel = nullptr;
    QStringList m_globalCompletions;
    std::map<QString, QStringList> m_moduleCompletions;

    // ======================================================================
    // Line number area
    // ======================================================================

    class LineNumberArea : public QWidget
    {
    public:
        explicit LineNumberArea(ScriptEditorWidget* editor) : QWidget(editor), m_editor(editor) {}
        QSize sizeHint() const override { return QSize(m_editor->lineNumberAreaWidth(), 0); }
    protected:
        void paintEvent(QPaintEvent* event) override
        {
            QPainter painter(this);
            painter.fillRect(event->rect(), QColor("#1e1e1e"));

            QTextBlock block = m_editor->firstVisibleBlock();
            int blockNumber = block.blockNumber();
            int top = qRound(m_editor->blockBoundingGeometry(block).translated(m_editor->contentOffset()).top());
            int bottom = top + qRound(m_editor->blockBoundingRect(block).height());
            int lineH = m_editor->fontMetrics().height();

            painter.setFont(m_editor->font());

            while (block.isValid() && top <= event->rect().bottom()) {
                if (block.isVisible() && bottom >= event->rect().top()) {
                    int lineNum = blockNumber + 1;

                    if (m_editor->m_breakpointLines.count(lineNum)) {
                        painter.setBrush(QColor("#e51400"));
                        painter.setPen(Qt::NoPen);
                        int circleSize = lineH - 4;
                        painter.drawEllipse(2, top + 2, circleSize, circleSize);
                    }

                    if (lineNum == m_editor->m_debugLine) {
                        painter.setPen(QColor("#ffcc00"));
                        painter.setBrush(QColor("#ffcc00"));
                        int arrowY = top + lineH / 2;
                        int arrowX = lineH - 2;
                        QPolygon arrow;
                        arrow << QPoint(arrowX - 6, arrowY - 4)
                              << QPoint(arrowX, arrowY)
                              << QPoint(arrowX - 6, arrowY + 4);
                        painter.drawPolygon(arrow);
                    }

                    QString number = QString::number(lineNum);
                    bool isCurrent = (blockNumber == m_editor->textCursor().blockNumber());
                    bool isDebug = (lineNum == m_editor->m_debugLine);
                    painter.setPen(isDebug ? QColor("#ffcc00") : (isCurrent ? QColor("#c6c6c6") : QColor("#606060")));
                    painter.drawText(0, top, width() - 4, lineH, Qt::AlignRight, number);
                }
                block = block.next();
                top = bottom;
                bottom = top + qRound(m_editor->blockBoundingRect(block).height());
                ++blockNumber;
            }
        }

        void mousePressEvent(QMouseEvent* event) override
        {
            int y = event->pos().y();
            QTextBlock block = m_editor->firstVisibleBlock();
            int top = qRound(m_editor->blockBoundingGeometry(block).translated(m_editor->contentOffset()).top());

            while (block.isValid()) {
                int bottom = top + qRound(m_editor->blockBoundingRect(block).height());
                if (y >= top && y < bottom) {
                    m_editor->toggleBreakpoint(block.blockNumber() + 1);
                    return;
                }
                block = block.next();
                top = bottom;
            }
        }

    private:
        ScriptEditorWidget* m_editor;
    };

    LineNumberArea* m_lineNumberArea;
};
