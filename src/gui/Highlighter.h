#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

#include "gui/DocumentText.h"

namespace placi::gui {

// Highlights the PlaciDocs dialect: headings and their {tags}, ::: sections,
// {halaman-baru}, citations, cross references, math, code and front matter.
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument* doc);
    void setDelimiters(const TagDelimiters& d);

protected:
    void highlightBlock(const QString& text) override;

private:
    enum State { Normal = 0, FrontMatter = 1, Code = 2, Math = 3 };
    void inline_(const QString& text, int from);

    TagDelimiters delims_;
    QTextCharFormat heading_, tag_, section_, cite_, xref_, math_, code_, front_, emph_, strong_, comment_, pagebreak_;
};

}  // namespace placi::gui
