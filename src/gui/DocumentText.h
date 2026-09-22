#pragma once
// Text-level edits of a PlaciDocs document that keep the author's formatting:
// front matter keys, heading tags and heading levels. No YAML round trip, so
// comments and ordering in the front matter survive.

#include <QString>
#include <QStringList>

namespace placi::gui {

struct TagDelimiters {
    QString open = QStringLiteral("{");
    QString close = QStringLiteral("}");
};

// ---- front matter

// Returns the value of a top-level scalar key, or an empty string.
QString frontMatterValue(const QString& doc, const QString& key);

// Sets (or adds) a top-level scalar key; creates the front matter if needed.
QString setFrontMatterValue(const QString& doc, const QString& key, const QString& value);

struct NewList {
    QString key;        // grafik
    QString title;      // DAFTAR GRAFIK
    QString prefix;     // Grafik
    QString label;      // gfk
    QString kind;       // float | figure | table | equation
    QString numbering;  // {h1}.{n}
};
// Adds (or replaces) an entry under `lists:` in the front matter.
QString addFrontMatterList(const QString& doc, const NewList& list);

// Quotes a YAML scalar when needed.
QString yamlScalar(const QString& value);

// ---- headings

struct HeadingLine {
    int level = 0;          // 0 = not a heading
    QString title;          // without the tag block
    QStringList tokens;     // words inside the tag block
};
HeadingLine parseHeadingLine(const QString& line, const TagDelimiters& d);
QString formatHeadingLine(const HeadingLine& h, const TagDelimiters& d);

// Adds the token if missing, removes it if present. Returns the new line.
QString toggleHeadingToken(const QString& line, const QString& token, const TagDelimiters& d);

// Changes a line into a heading of `level` (0 = plain paragraph), keeping its tags.
QString setHeadingLevel(const QString& line, int level, const TagDelimiters& d);

// A label id derived from text: "Arsitektur Sistem" -> "arsitektur-sistem".
QString slugify(const QString& text);

}  // namespace placi::gui
