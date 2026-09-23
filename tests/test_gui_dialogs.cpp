// Drives the insert dialogs without a screen (QT_QPA_PLATFORM=offscreen):
// fills their fields and checks the Markdown they produce.
#include <doctest.h>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTemporaryDir>

#include "gui/Dialogs.h"

using namespace placi::gui;

namespace {

// One QApplication for the whole test run.
void ensureApp() {
    static int argc = 1;
    static char name[] = "placi_gui_tests";
    static char* argv[] = {name, nullptr};
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef PLACI_QT_PLUGINS
        qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QByteArray(PLACI_QT_PLUGINS "/platforms"));
        QCoreApplication::addLibraryPath(QStringLiteral(PLACI_QT_PLUGINS));
#endif
        return new QApplication(argc, argv);
    }();
    (void)app;
}

QList<ListChoice> lists() {
    return {{"gambar", "fig", "Gambar", "figure"},
            {"tabel", "tbl", "Tabel", "table"},
            {"rumus", "eq", "Rumus", "equation"},
            {"grafik", "gfk", "Grafik", "float"}};
}

template <class W>
W* field(QDialog& d, const char* name) {
    W* w = d.findChild<W*>(QString::fromLatin1(name));
    REQUIRE(w != nullptr);
    return w;
}

const TagDelimiters kBraces;

}  // namespace

TEST_CASE("insert image dialog") {
    ensureApp();
    InsertImageDialog d(QDir::tempPath(), lists(), kBraces);
    field<QLineEdit>(d, "file")->setText("img/arsitektur.png");
    field<QLineEdit>(d, "caption")->setText("Arsitektur sistem");

    // The figure list comes first, so images land in "Daftar Gambar" by default.
    CHECK(field<QComboBox>(d, "list")->currentData().toString() == "fig");
    CHECK(d.markdown() == "![Arsitektur sistem](img/arsitektur.png){#fig:arsitektur}");

    field<QSpinBox>(d, "width")->setValue(60);
    field<QLineEdit>(d, "label")->setText("utama");
    CHECK(d.markdown() == "![Arsitektur sistem](img/arsitektur.png){#fig:utama width=60%}");

    // A user list of kind float is offered too.
    auto* list = field<QComboBox>(d, "list");
    list->setCurrentIndex(list->findData("gfk"));
    CHECK(d.markdown() == "![Arsitektur sistem](img/arsitektur.png){#gfk:utama width=60%}");
}

TEST_CASE("insert table dialog") {
    ensureApp();
    InsertTableDialog d(lists(), kBraces);
    CHECK(field<QComboBox>(d, "list")->currentData().toString() == "tbl");  // table list first

    field<QSpinBox>(d, "rows")->setValue(2);
    field<QSpinBox>(d, "cols")->setValue(2);
    CHECK(d.markdown() == "| Kolom 1 | Kolom 2 |\n|---|---|\n|   |   |\n|   |   |");

    field<QLineEdit>(d, "caption")->setText("Hasil uji");
    CHECK(d.markdown() == "Tabel: Hasil uji {#tbl:hasil-uji}\n\n| Kolom 1 | Kolom 2 |\n|---|---|\n|   |   |\n|   |   |");
}

TEST_CASE("insert math dialog") {
    ensureApp();
    InsertMathDialog d(kBraces);
    field<QPlainTextEdit>(d, "latex")->setPlainText("a &= b \\\\\nc &= d");
    field<QLineEdit>(d, "caption")->setText("Skor akhir");
    CHECK(d.markdown() == "```math {#eq:skor-akhir caption=\"Skor akhir\"}\na &= b \\\\\nc &= d\n```");

    field<QLineEdit>(d, "label")->setText("skor");
    CHECK(d.markdown().startsWith("```math {#eq:skor caption=\"Skor akhir\"}"));

    field<QCheckBox>(d, "numbered")->setChecked(false);
    CHECK(d.markdown().startsWith("```math {.unnumbered}"));

    field<QCheckBox>(d, "inline")->setChecked(true);
    CHECK(d.markdown() == "$a &= b \\\\ c &= d$");
}

TEST_CASE("Tambah Daftar dialog fills in sensible defaults") {
    ensureApp();
    AddListDialog d;
    field<QLineEdit>(d, "name")->setText("grafik");
    auto l = d.list();
    CHECK(l.key == "grafik");
    CHECK(l.title == "DAFTAR GRAFIK");
    CHECK(l.prefix == "Grafik");
    CHECK(l.label == "gra");
    CHECK(l.kind == "float");
    CHECK(l.numbering == "{h1}.{n}");
    CHECK(d.insertHeading());

    field<QLineEdit>(d, "label")->setText("gfk");
    field<QComboBox>(d, "kind")->setCurrentIndex(field<QComboBox>(d, "kind")->findData("figure"));
    CHECK(d.list().label == "gfk");
    CHECK(d.list().kind == "figure");
}

TEST_CASE("citation dialog reads the .bib and writes citations") {
    ensureApp();
    QTemporaryDir tmp;
    REQUIRE(tmp.isValid());
    const QString bib = QDir(tmp.path()).filePath("refs.bib");
    {
        QFile f(bib);
        REQUIRE(f.open(QIODevice::WriteOnly));
        f.write("@book{knuth1984, author={Knuth, Donald E.}, title={The TeXbook}, year={1984}}\n"
                "@article{doe2020, author={Doe, Jane}, title={A Study}, year={2020}}\n");
    }
    CitationDialog d(bib);
    auto* entries = field<QListWidget>(d, "entries");
    REQUIRE(entries->count() == 2);

    // Entries keep the order of the .bib file.
    CHECK(entries->item(0)->data(Qt::UserRole).toString() == "knuth1984");
    CHECK(entries->item(0)->text().contains("Knuth, Donald E. (1984)"));

    entries->setCurrentRow(0);
    CHECK(d.markdown() == "[@knuth1984]");

    // Several selected entries become one citation group.
    entries->selectAll();
    CHECK(d.markdown() == "[@knuth1984; @doe2020]");
    entries->setCurrentRow(0);

    field<QLineEdit>(d, "locator")->setText("hlm. 5");
    CHECK(d.markdown() == "[@knuth1984, hlm. 5]");

    auto* mode = field<QComboBox>(d, "mode");
    mode->setCurrentIndex(mode->findData("text"));
    CHECK(d.markdown() == "@knuth1984");
    mode->setCurrentIndex(mode->findData("year"));
    CHECK(d.markdown() == "[-@knuth1984, hlm. 5]");

    // Searching filters the list.
    field<QLineEdit>(d, "search")->setText("Study");
    CHECK(entries->count() == 1);
}
