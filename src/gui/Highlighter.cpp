#include "gui/Highlighter.h"

#include <QGuiApplication>
#include <QPalette>

namespace placi::gui {

Highlighter::Highlighter(QTextDocument* doc) : QSyntaxHighlighter(doc) {
    // Colours that read well on both light and dark system themes.
    const bool dark = QGuiApplication::palette().color(QPalette::Base).lightness() < 128;
    auto c = [dark](const char* light, const char* darkc) { return QColor(QLatin1String(dark ? darkc : light)); };
    heading_.setFontWeight(QFont::Bold);
    heading_.setForeground(c("#1f4e9c", "#82b1ff"));
    tag_.setForeground(c("#b34d00", "#ffb86c"));
    tag_.setBackground(c("#fff1e0", "#3d2a12"));
    section_.setForeground(c("#7a1fa2", "#d7a7ff"));
    section_.setFontWeight(QFont::Bold);
    section_.setBackground(c("#f3e8fa", "#33203f"));
    pagebreak_ = section_;
    cite_.setForeground(c("#0b7a3e", "#6fd08c"));
    xref_.setForeground(c("#007a87", "#5fd0dc"));
    xref_.setFontUnderline(true);
    math_.setForeground(c("#8a2b5c", "#ff9ac7"));
    code_.setForeground(c("#555555", "#c8c8c8"));
    code_.setBackground(c("#f2f2f2", "#2b2b2b"));
    front_.setForeground(c("#6b6b6b", "#9a9a9a"));
    emph_.setFontItalic(true);
    strong_.setFontWeight(QFont::Bold);
    comment_.setForeground(c("#9a9a9a", "#777777"));
}

void Highlighter::setDelimiters(const TagDelimiters& d) {
    if (d.open == delims_.open && d.close == delims_.close) return;
    delims_ = d;
    rehighlight();
}

void Highlighter::highlightBlock(const QString& text) {
    const int prev = previousBlockState();
    const QString t = text.trimmed();
    setCurrentBlockState(Normal);

    // Front matter: only at the very top of the document.
    if ((prev == FrontMatter) || (currentBlock().blockNumber() == 0 && t == QLatin1String("---"))) {
        setFormat(0, text.size(), front_);
        bool closing = prev == FrontMatter && (t == QLatin1String("---") || t == QLatin1String("..."));
        setCurrentBlockState(closing ? Normal : FrontMatter);
        return;
    }
    // Fenced code / math blocks.
    if (prev == Code || prev == Math) {
        setFormat(0, text.size(), prev == Math ? math_ : code_);
        if (!t.startsWith(QLatin1String("```")) && !t.startsWith(QLatin1String("~~~"))) setCurrentBlockState(prev);
        return;
    }
    if (t.startsWith(QLatin1String("```")) || t.startsWith(QLatin1String("~~~"))) {
        bool math = t.mid(3).trimmed().startsWith(QLatin1String("math"));
        setFormat(0, text.size(), math ? math_ : code_);
        // highlight the attribute block of ```math {#eq:x}
        int open = text.indexOf(delims_.open);
        if (open >= 0) setFormat(open, text.size() - open, tag_);
        setCurrentBlockState(math ? Math : Code);
        return;
    }
    // ::: Section
    static const QRegularExpression sectionRe(QStringLiteral(R"(^\s{0,3}:{3,}\s*[\w-]*\s*$)"));
    if (sectionRe.match(text).hasMatch()) {
        setFormat(0, text.size(), section_);
        return;
    }
    // {halaman-baru}
    if (t == delims_.open + QLatin1String("halaman-baru") + delims_.close ||
        t == delims_.open + QLatin1String("pagebreak") + delims_.close) {
        setFormat(0, text.size(), pagebreak_);
        return;
    }
    // Headings and their tag block.
    auto h = parseHeadingLine(text, delims_);
    if (h.level) {
        setFormat(0, text.size(), heading_);
        if (!h.tokens.isEmpty()) {
            int open = text.lastIndexOf(delims_.open);
            if (open >= 0) setFormat(open, text.size() - open, tag_);
        }
        return;
    }
    inline_(text, 0);
}

void Highlighter::inline_(const QString& text, int from) {
    struct Rule {
        QRegularExpression re;
        const QTextCharFormat* fmt;
    };
    static const QRegularExpression strong(QStringLiteral(R"(\*\*[^*]+\*\*|__[^_]+__)"));
    static const QRegularExpression emph(QStringLiteral(R"((?<![*\w])\*[^*\s][^*]*\*(?!\*)|(?<![_\w])_[^_\s][^_]*_(?!_))"));
    static const QRegularExpression code(QStringLiteral(R"(`[^`]+`)"));
    static const QRegularExpression math(QStringLiteral(R"(\$\$?[^$]+\$\$?)"));
    static const QRegularExpression cite(QStringLiteral(R"(\[-?@[^\]]+\]|(?<![\w@])@[A-Za-z0-9_][\w:.\-/]*)"));
    static const QRegularExpression comment(QStringLiteral(R"(<!--.*?-->)"));
    const Rule rules[] = {{strong, &strong_}, {emph, &emph_}, {math, &math_}, {cite, &cite_}, {code, &code_}, {comment, &comment_}};
    for (const auto& r : rules) {
        auto it = r.re.globalMatch(text, from);
        while (it.hasNext()) {
            auto m = it.next();
            const QTextCharFormat* f = r.fmt;
            // @fig:x style cross references look different from citations.
            if (r.fmt == &cite_ && m.captured().contains(':') && !m.captured().startsWith('[')) f = &xref_;
            setFormat(m.capturedStart(), m.capturedLength(), *f);
        }
    }
}

}  // namespace placi::gui
