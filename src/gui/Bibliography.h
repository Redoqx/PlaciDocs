#pragma once
// Minimal BibTeX reader for the citation picker (key, type, author, title, year).

#include <QList>
#include <QString>

namespace placi::gui {

struct BibEntry {
    QString key;
    QString type;
    QString author;
    QString title;
    QString year;
};

QList<BibEntry> parseBibtex(const QString& text);
QList<BibEntry> readBibFile(const QString& path);

// Appends an entry to a .bib file (creating it if needed).
bool appendBibEntry(const QString& path, const BibEntry& e, QString* error = nullptr);

}  // namespace placi::gui
