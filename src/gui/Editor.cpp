#include "gui/Editor.h"

#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>

namespace placi::gui {

namespace {
class Gutter : public QWidget {
public:
    explicit Gutter(Editor* editor) : QWidget(editor), editor_(editor) {}
    QSize sizeHint() const override { return {editor_->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { editor_->paintGutter(event); }

private:
    Editor* editor_;
};
}  // namespace

Editor::Editor(QWidget* parent) : QPlainTextEdit(parent), gutter_(new Gutter(this)) {
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(11);
    setFont(mono);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabStopDistance(fontMetrics().horizontalAdvance(' ') * 4);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateGutterWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this, &Editor::updateGutter);
    updateGutterWidth();
}

int Editor::gutterWidth() const {
    int digits = QString::number(qMax(1, blockCount())).size();
    return 12 + fontMetrics().horizontalAdvance('9') * qMax(3, digits);
}

void Editor::updateGutterWidth() { setViewportMargins(gutterWidth(), 0, 0, 0); }

void Editor::updateGutter(const QRect& rect, int dy) {
    if (dy) gutter_->scroll(0, dy);
    else gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateGutterWidth();
}

void Editor::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    QRect cr = contentsRect();
    gutter_->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void Editor::paintGutter(QPaintEvent* event) {
    QPainter p(gutter_);
    p.fillRect(event->rect(), palette().alternateBase());
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            p.setPen(number == current ? palette().text().color() : palette().placeholderText().color());
            p.drawText(0, top, gutter_->width() - 6, fontMetrics().height(), Qt::AlignRight, QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void Editor::keyPressEvent(QKeyEvent* event) {
    // Continue bullet / numbered lists on Enter.
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !event->modifiers()) {
        const QString line = currentLineText();
        static const QRegularExpression bullet(QStringLiteral(R"(^(\s*)([-*+]|\d+[.)])\s+(.*)$)"));
        auto m = bullet.match(line);
        if (m.hasMatch()) {
            if (m.captured(3).isEmpty()) {  // empty item ends the list
                replaceCurrentLine(QString());
                return;
            }
            QString marker = m.captured(2);
            if (marker[0].isDigit()) marker = QString::number(marker.chopped(1).toInt() + 1) + marker.back();
            QPlainTextEdit::keyPressEvent(event);
            insertPlainText(m.captured(1) + marker + ' ');
            return;
        }
    }
    QPlainTextEdit::keyPressEvent(event);
}

int Editor::currentLineNumber() const { return textCursor().blockNumber() + 1; }

QString Editor::currentLineText() const { return textCursor().block().text(); }

void Editor::replaceCurrentLine(const QString& text) {
    QTextCursor c = textCursor();
    c.movePosition(QTextCursor::StartOfBlock);
    c.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    c.insertText(text);
    setTextCursor(c);
}

void Editor::goToLine(int line) {
    QTextBlock b = document()->findBlockByNumber(qMax(0, line - 1));
    if (!b.isValid()) return;
    QTextCursor c(b);
    setTextCursor(c);
    centerCursor();
    setFocus();
}

void Editor::wrapSelection(const QString& before, const QString& after) {
    QTextCursor c = textCursor();
    QString sel = c.selectedText();
    c.insertText(before + sel + after);
    if (sel.isEmpty()) {
        c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, after.size());
        setTextCursor(c);
    }
}

void Editor::insertBlock(const QString& markdown) {
    QTextCursor c = textCursor();
    c.beginEditBlock();
    if (!c.block().text().trimmed().isEmpty()) {
        c.movePosition(QTextCursor::EndOfBlock);
        c.insertText(QStringLiteral("\n\n"));
    } else if (c.block().previous().isValid() && !c.block().previous().text().trimmed().isEmpty()) {
        c.insertText(QStringLiteral("\n"));
    }
    c.insertText(markdown + QStringLiteral("\n"));
    c.endEditBlock();
    setTextCursor(c);
    setFocus();
}

void Editor::wrapSelectedLines(const QString& firstLine, const QString& lastLine) {
    QTextCursor c = textCursor();
    const int start = c.selectionStart(), end = c.selectionEnd();
    c.beginEditBlock();  // one undo step
    c.setPosition(end);
    c.movePosition(QTextCursor::EndOfBlock);
    c.insertText('\n' + lastLine);
    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(firstLine + '\n');
    c.endEditBlock();
}

}  // namespace placi::gui
