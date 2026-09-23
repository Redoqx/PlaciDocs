// Tests for the document edits the GUI performs (front matter, heading tags)
// and for the BibTeX reader behind the citation picker. Built only with Qt.
#include <doctest.h>

#include <QString>

#include "gui/Bibliography.h"
#include "gui/DocumentText.h"

using namespace placi::gui;

namespace {
const TagDelimiters kBraces;
QString s(const char* x) { return QString::fromUtf8(x); }
}  // namespace

TEST_CASE("front matter values are read, replaced and added") {
    const QString doc = s("---\ntitle: Judul\nstyle: skripsi-umum\n---\n\n# Bab\n");
    CHECK(frontMatterValue(doc, "title") == "Judul");
    CHECK(frontMatterValue(doc, "author").isEmpty());

    auto changed = setFrontMatterValue(doc, "style", "laporan-kantor");
    CHECK(frontMatterValue(changed, "style") == "laporan-kantor");
    CHECK(changed.count(s("style:")) == 1);

    auto added = setFrontMatterValue(doc, "bibliography", "references.bib");
    CHECK(frontMatterValue(added, "bibliography") == "references.bib");
    CHECK(added.endsWith(s("# Bab\n")));  // the body is untouched

    // A document without front matter gets one.
    auto fresh = setFrontMatterValue(s("# Bab\n"), "title", "Baru");
    CHECK(fresh.startsWith(s("---\ntitle: Baru\n---\n")));
    CHECK(frontMatterValue(fresh, "title") == "Baru");

    // Values that YAML would misread are quoted.
    auto tricky = setFrontMatterValue(doc, "title", "Judul: dua {bagian}");
    CHECK(tricky.contains(s("title: \"Judul: dua {bagian}\"")));
    CHECK(frontMatterValue(tricky, "title") == "Judul: dua {bagian}");
}

TEST_CASE("Tambah Daftar writes a list into the front matter") {
    NewList l{s("grafik"), s("DAFTAR GRAFIK"), s("Grafik"), s("gfk"), s("float"), s("{h1}.{n}")};

    auto doc = addFrontMatterList(s("---\ntitle: Judul\n---\n\nIsi.\n"), l);
    CHECK(doc.contains(s("lists:")));
    CHECK(doc.contains(s("  grafik: { title: DAFTAR GRAFIK, prefix: Grafik, label: gfk, kind: float, numbering: \"{h1}.{n}\" }")));
    CHECK(doc.endsWith(s("Isi.\n")));

    // A second list joins the same block; adding the same key replaces it.
    NewList other{s("peta"), s("DAFTAR PETA"), s("Peta"), s("pta"), s("float"), {}};
    auto two = addFrontMatterList(doc, other);
    CHECK(two.contains(s("  grafik: {")));
    CHECK(two.contains(s("  peta: {")));
    CHECK(two.count(s("lists:")) == 1);

    NewList renamed{s("grafik"), s("GRAFIK"), s("Grafik"), s("gfk"), s("figure"), {}};
    auto replaced = addFrontMatterList(two, renamed);
    CHECK(replaced.count(s("grafik: {")) == 1);
    CHECK(replaced.contains(s("kind: figure")));
}

TEST_CASE("heading lines: levels and tags") {
    auto h = parseHeadingLine(s("## Latar Belakang {pembuka -}"), kBraces);
    CHECK(h.level == 2);
    CHECK(h.title == "Latar Belakang");
    CHECK(h.tokens == QStringList{s("pembuka"), s("-")});

    CHECK(parseHeadingLine(s("Bukan judul"), kBraces).level == 0);
    // An escaped delimiter is part of the title, not a tag.
    auto escaped = parseHeadingLine(s("# Rute \\{home\\}"), kBraces);
    CHECK(escaped.level == 1);
    CHECK(escaped.tokens.isEmpty());

    CHECK(toggleHeadingToken(s("# Bab"), s("lampiran"), kBraces) == "# Bab {lampiran}");
    CHECK(toggleHeadingToken(s("# Bab {lampiran}"), s("lampiran"), kBraces) == "# Bab");
    CHECK(toggleHeadingToken(s("# Bab {pembuka}"), s("daftar gambar"), kBraces) == "# Bab {pembuka daftar gambar}");
    CHECK(toggleHeadingToken(s("# Bab {pembuka daftar gambar}"), s("daftar gambar"), kBraces) == "# Bab {pembuka}");
    CHECK(toggleHeadingToken(s("Paragraf"), s("lampiran"), kBraces) == "Paragraf");  // only headings carry tags

    CHECK(setHeadingLevel(s("Paragraf biasa"), 2, kBraces) == "## Paragraf biasa");
    CHECK(setHeadingLevel(s("### Judul {pembuka}"), 1, kBraces) == "# Judul {pembuka}");
    CHECK(setHeadingLevel(s("### Judul {pembuka}"), 0, kBraces) == "Judul {pembuka}");
}

TEST_CASE("custom tag delimiters") {
    const TagDelimiters square{s("[["), s("]]")};
    auto h = parseHeadingLine(s("# Praktikum {home} [[lampiran]]"), square);
    CHECK(h.title == "Praktikum {home}");
    CHECK(h.tokens == QStringList{s("lampiran")});
    CHECK(toggleHeadingToken(s("# Bab"), s("pembuka"), square) == "# Bab [[pembuka]]");
}

TEST_CASE("slugify makes labels") {
    CHECK(slugify(s("Arsitektur Sistem")) == "arsitektur-sistem");
    CHECK(slugify(s("Tabel 1: Hasil Uji!")) == "tabel-1-hasil-uji");
    CHECK(slugify(s("   ")).isEmpty());
}

TEST_CASE("BibTeX entries are read for the citation picker") {
    auto entries = parseBibtex(s(R"(
@comment{ignored}
@book{knuth1984,
  author    = {Knuth, Donald E.},
  title     = {The {\TeX}book},
  publisher = {Addison-Wesley},
  year      = {1984}
}
@article{doe2020, author = "Doe, Jane", title = "A Study", year = 2020}
)"));
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].key == "knuth1984");
    CHECK(entries[0].type == "book");
    CHECK(entries[0].author == "Knuth, Donald E.");
    CHECK(entries[0].title.contains(s("book")));
    CHECK(entries[0].year == "1984");
    CHECK(entries[1].key == "doe2020");
    CHECK(entries[1].author == "Doe, Jane");
    CHECK(entries[1].year == "2020");
}
