// ScriptEditorWidget.h - Code editor with line numbers and current-line highlight
#pragma once

#include <QPlainTextEdit>
#include <QPainter>
#include <QTextBlock>

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
    }

    int lineNumberAreaWidth() const
    {
        int digits = 1;
        int max = qMax(1, blockCount());
        while (max >= 10) { max /= 10; ++digits; }
        return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QPlainTextEdit::resizeEvent(event);
        QRect cr = contentsRect();
        m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Tab) {
            insertPlainText("    ");
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            // Auto-indent: copy leading whitespace from current line
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
            QTextEdit::ExtraSelection selection;
            selection.format.setBackground(QColor("#2a2d2e"));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            extraSelections.append(selection);
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

private:
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

            painter.setFont(m_editor->font());

            while (block.isValid() && top <= event->rect().bottom()) {
                if (block.isVisible() && bottom >= event->rect().top()) {
                    QString number = QString::number(blockNumber + 1);
                    bool isCurrent = (blockNumber == m_editor->textCursor().blockNumber());
                    painter.setPen(isCurrent ? QColor("#c6c6c6") : QColor("#606060"));
                    painter.drawText(0, top, width() - 4, m_editor->fontMetrics().height(),
                                     Qt::AlignRight, number);
                }
                block = block.next();
                top = bottom;
                bottom = top + qRound(m_editor->blockBoundingRect(block).height());
                ++blockNumber;
            }
        }
    private:
        ScriptEditorWidget* m_editor;
    };

    LineNumberArea* m_lineNumberArea;
};
