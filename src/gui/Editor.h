#pragma once

#include <QPlainTextEdit>

namespace placi::gui {

// Plain-text Markdown editor with line numbers and helpers for the toolbar.
class Editor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit Editor(QWidget* parent = nullptr);

    int currentLineNumber() const;  // 1-based
    QString currentLineText() const;
    void replaceCurrentLine(const QString& text);
    void goToLine(int line);        // 1-based
    void wrapSelection(const QString& before, const QString& after);  // inline, e.g. ** **
    void insertBlock(const QString& markdown);  // on its own lines, separated by blank lines
    void wrapSelectedLines(const QString& firstLine, const QString& lastLine);

    // line number gutter
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void updateGutterWidth();
    void updateGutter(const QRect& rect, int dy);
    QWidget* gutter_;
};

}  // namespace placi::gui
