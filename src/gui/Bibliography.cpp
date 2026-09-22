#include "gui/Bibliography.h"

#include <QFile>
#include <QTextStream>

namespace placi::gui {

namespace {

// Reads a {...} or "..." value starting at text[i]; i ends after it.
QString readValue(const QString& text, int& i) {
    while (i < text.size() && text[i].isSpace()) ++i;
    if (i >= text.size()) return {};
    QString out;
    if (text[i] == '{') {
        int depth = 0;
        for (; i < text.size(); ++i) {
            QChar c = text[i];
            if (c == '{') {
                if (depth++ == 0) continue;
            } else if (c == '}') {
                if (--depth == 0) {
                    ++i;
                    break;
                }
            }
            out += c;
        }
    } else if (text[i] == '"') {
        for (++i; i < text.size() && text[i] != '"'; ++i) out += text[i];
        ++i;
    } else {
        while (i < text.size() && text[i] != ',' && text[i] != '}') out += text[i++];
    }
    return out.simplified().remove('{').remove('}');
}

}  // namespace

QList<BibEntry> parseBibtex(const QString& text) {
    QList<BibEntry> out;
    int i = 0;
    while ((i = text.indexOf('@', i)) >= 0) {
        int brace = text.indexOf('{', i);
        if (brace < 0) break;
        BibEntry e;
        e.type = text.mid(i + 1, brace - i - 1).trimmed().toLower();
        if (e.type == QLatin1String("comment") || e.type == QLatin1String("string") || e.type == QLatin1String("preamble")) {
            i = brace + 1;
            continue;
        }
        int comma = text.indexOf(',', brace);
        if (comma < 0) break;
        e.key = text.mid(brace + 1, comma - brace - 1).trimmed();
        i = comma + 1;
        // fields until the closing brace of the entry
        while (i < text.size()) {
            while (i < text.size() && (text[i].isSpace() || text[i] == ',')) ++i;
            if (i >= text.size() || text[i] == '}') {
                ++i;
                break;
            }
            int eq = text.indexOf('=', i);
            if (eq < 0) break;
            QString name = text.mid(i, eq - i).trimmed().toLower();
            i = eq + 1;
            QString value = readValue(text, i);
            if (name == QLatin1String("author")) e.author = value;
            else if (name == QLatin1String("title")) e.title = value;
            else if (name == QLatin1String("year")) e.year = value;
        }
        if (!e.key.isEmpty()) out.append(e);
    }
    return out;
}

QList<BibEntry> readBibFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return parseBibtex(QString::fromUtf8(f.readAll()));
}

bool appendBibEntry(const QString& path, const BibEntry& e, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        if (error) *error = f.errorString();
        return false;
    }
    QTextStream s(&f);
    s << "\n@" << (e.type.isEmpty() ? QStringLiteral("misc") : e.type) << "{" << e.key << ",\n";
    if (!e.author.isEmpty()) s << "  author = {" << e.author << "},\n";
    if (!e.title.isEmpty()) s << "  title  = {" << e.title << "},\n";
    if (!e.year.isEmpty()) s << "  year   = {" << e.year << "},\n";
    s << "}\n";
    return true;
}

}  // namespace placi::gui
