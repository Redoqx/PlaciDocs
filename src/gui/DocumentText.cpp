#include "gui/DocumentText.h"

#include <QRegularExpression>

namespace placi::gui {

namespace {

struct FrontMatter {
    int start = -1;  // index of the first line after the opening ---
    int end = -1;    // index of the closing --- line
};

FrontMatter findFrontMatter(const QStringList& lines) {
    FrontMatter fm;
    if (lines.isEmpty() || lines[0].trimmed() != QLatin1String("---")) return fm;
    for (int i = 1; i < lines.size(); ++i) {
        auto t = lines[i].trimmed();
        if (t == QLatin1String("---") || t == QLatin1String("...")) {
            fm.start = 1;
            fm.end = i;
            return fm;
        }
    }
    return fm;
}

bool isTopLevelKey(const QString& line, const QString& key) {
    return line.startsWith(key + QLatin1Char(':')) ;
}

QString unquote(QString v) {
    v = v.trimmed();
    if (v.size() >= 2 && ((v.startsWith('"') && v.endsWith('"')) || (v.startsWith('\'') && v.endsWith('\''))))
        v = v.mid(1, v.size() - 2);
    return v;
}

}  // namespace

QString yamlScalar(const QString& value) {
    static const QRegularExpression special(QStringLiteral(R"([:#\[\]{},&*!|>'"%@`]|^\s|\s$|^-)"));
    if (value.isEmpty() || value.contains(special)) {
        QString v = value;
        v.replace('\\', QStringLiteral("\\\\")).replace('"', QStringLiteral("\\\""));
        return '"' + v + '"';
    }
    return value;
}

QString frontMatterValue(const QString& doc, const QString& key) {
    const auto lines = doc.split('\n');
    auto fm = findFrontMatter(lines);
    for (int i = fm.start; fm.start >= 0 && i < fm.end; ++i)
        if (isTopLevelKey(lines[i], key)) return unquote(lines[i].mid(key.size() + 1));
    return {};
}

QString setFrontMatterValue(const QString& doc, const QString& key, const QString& value) {
    auto lines = doc.split('\n');
    auto fm = findFrontMatter(lines);
    const QString entry = key + QStringLiteral(": ") + yamlScalar(value);
    if (fm.start < 0) {
        lines.prepend(QString());
        lines.prepend(QStringLiteral("---"));
        lines.prepend(entry);
        lines.prepend(QStringLiteral("---"));
        return lines.join('\n');
    }
    for (int i = fm.start; i < fm.end; ++i) {
        if (isTopLevelKey(lines[i], key)) {
            lines[i] = entry;
            return lines.join('\n');
        }
    }
    lines.insert(fm.end, entry);
    return lines.join('\n');
}

QString addFrontMatterList(const QString& doc, const NewList& l) {
    QStringList fields;
    auto add = [&](const char* k, const QString& v) {
        if (!v.isEmpty()) fields << QString::fromLatin1(k) + QStringLiteral(": ") + yamlScalar(v);
    };
    add("title", l.title);
    add("prefix", l.prefix);
    add("label", l.label);
    add("kind", l.kind);
    add("numbering", l.numbering);
    const QString entry = QStringLiteral("  ") + l.key + QStringLiteral(": { ") + fields.join(QStringLiteral(", ")) + QStringLiteral(" }");

    QString text = doc;
    auto lines = text.split('\n');
    auto fm = findFrontMatter(lines);
    if (fm.start < 0) {
        text = setFrontMatterValue(text, QStringLiteral("lists"), QString());  // creates the block
        lines = text.split('\n');
        fm = findFrontMatter(lines);
        for (int i = fm.start; i < fm.end; ++i)
            if (isTopLevelKey(lines[i], QStringLiteral("lists"))) lines[i] = QStringLiteral("lists:");
    }
    int listsLine = -1;
    for (int i = fm.start; i < fm.end; ++i)
        if (isTopLevelKey(lines[i], QStringLiteral("lists"))) listsLine = i;
    if (listsLine < 0) {
        lines.insert(fm.end, QStringLiteral("lists:"));
        listsLine = fm.end;
        fm.end += 1;
    }
    // Replace an existing entry with the same key, else append after the block.
    int insertAt = listsLine + 1;
    for (int i = listsLine + 1; i < fm.end; ++i) {
        if (!lines[i].startsWith(' ') && !lines[i].trimmed().isEmpty()) break;
        if (lines[i].trimmed().startsWith(l.key + QLatin1Char(':'))) {
            lines[i] = entry;
            return lines.join('\n');
        }
        insertAt = i + 1;
    }
    lines.insert(insertAt, entry);
    return lines.join('\n');
}

HeadingLine parseHeadingLine(const QString& line, const TagDelimiters& d) {
    HeadingLine h;
    int n = 0;
    while (n < line.size() && line[n] == '#') ++n;
    if (n == 0 || n > 6 || (n < line.size() && line[n] != ' ')) return h;
    h.level = n;
    QString rest = line.mid(n).trimmed();
    if (rest.endsWith(d.close)) {
        int open = rest.lastIndexOf(d.open, rest.size() - d.close.size() - 1);
        // "\{" is an escaped delimiter, not a tag block.
        if (open >= 0 && !(open > 0 && rest[open - 1] == '\\')) {
            QString body = rest.mid(open + d.open.size(), rest.size() - d.close.size() - open - d.open.size());
            h.tokens = body.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            rest = rest.left(open).trimmed();
        }
    }
    h.title = rest;
    return h;
}

QString formatHeadingLine(const HeadingLine& h, const TagDelimiters& d) {
    QString line = QString(h.level, '#') + ' ' + h.title;
    if (!h.tokens.isEmpty()) line += ' ' + d.open + h.tokens.join(' ') + d.close;
    return line;
}

QString toggleHeadingToken(const QString& line, const QString& token, const TagDelimiters& d) {
    auto h = parseHeadingLine(line, d);
    if (!h.level) return line;
    const QStringList words = token.split(' ', Qt::SkipEmptyParts);
    // Multi-word tokens ("daftar gambar") are matched as a sequence.
    int at = -1;
    for (int i = 0; i + words.size() <= h.tokens.size(); ++i)
        if (h.tokens.mid(i, words.size()) == words) at = i;
    if (at >= 0) {
        h.tokens.remove(at, words.size());
    } else {
        h.tokens += words;
    }
    return formatHeadingLine(h, d);
}

QString setHeadingLevel(const QString& line, int level, const TagDelimiters& d) {
    auto h = parseHeadingLine(line, d);
    if (!h.level) {
        if (level == 0) return line;
        h.title = line.trimmed();
        h.tokens.clear();
    }
    if (level == 0) {
        QString plain = h.title;
        if (!h.tokens.isEmpty()) plain += ' ' + d.open + h.tokens.join(' ') + d.close;
        return plain;
    }
    h.level = level;
    return formatHeadingLine(h, d);
}

QString slugify(const QString& text) {
    QString s = text.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    bool dash = false;
    for (QChar c : s) {
        if (c.isLetterOrNumber() && c.unicode() < 128) {
            out += c;
            dash = false;
        } else if (!dash && !out.isEmpty()) {
            out += '-';
            dash = true;
        }
    }
    while (out.endsWith('-')) out.chop(1);
    return out.left(40);
}

}  // namespace placi::gui
