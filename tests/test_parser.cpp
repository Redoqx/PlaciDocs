#include <doctest.h>

#include "core/passes/passes.hpp"
#include "core/pipeline.hpp"

using namespace placi;

namespace {
Document parse(std::string_view md) {
    Diagnostics d;
    return parse_document(md, d, "test.md");
}
const Node& first(const Document& doc) { return doc.root.children.at(0); }
}  // namespace

TEST_CASE("front matter becomes metadata") {
    auto doc = parse("---\ntitle: Judul Skripsi\nauthor: [Ani, Budi]\nstyle: skripsi-umum\n---\n\nIsi.\n");
    CHECK(doc.meta["title"] == "Judul Skripsi");
    CHECK(doc.meta["author"] == "Ani, Budi");
    CHECK(doc.meta["style"] == "skripsi-umum");
    REQUIRE(doc.root.children.size() == 1);
    CHECK(first(doc).type == NodeType::Paragraph);
}

TEST_CASE("heading attributes") {
    auto doc = parse("# Pendahuluan {#sec:intro}\n\n## Kata Pengantar {-}\n");
    REQUIRE(doc.root.children.size() == 2);
    auto& h1 = doc.root.children[0];
    CHECK(h1.level == 1);
    CHECK(h1.attr("id") == "sec:intro");
    CHECK(plain_text(h1) == "Pendahuluan");
    CHECK(doc.root.children[1].has_class("unnumbered"));
}

TEST_CASE("lone image paragraph becomes a figure") {
    auto doc = parse("![Arsitektur sistem](img/arsitektur.png){#fig:arsitektur width=70%}\n");
    auto& fig = first(doc);
    REQUIRE(fig.type == NodeType::Figure);
    CHECK(fig.attr("src") == "img/arsitektur.png");
    CHECK(fig.attr("id") == "fig:arsitektur");
    CHECK(fig.attr("width") == "70%");
    CHECK(plain_text(Node{NodeType::Paragraph, ""}).empty());
    REQUIRE(!fig.caption.empty());
    CHECK(fig.caption[0].text == "Arsitektur sistem");
}

TEST_CASE("table with caption and attributes") {
    auto doc = parse("Tabel: Data responden {#tbl:resp}\n\n| A | B | C |\n|:--|:-:|--:|\n| 1 | 2 | 3 |\n| 4 | 5 | 6 |\n");
    REQUIRE(doc.root.children.size() == 1);
    auto& t = first(doc);
    REQUIRE(t.type == NodeType::Table);
    CHECK(t.column_count() == 3);
    CHECK(t.body_row_count() == 2);
    CHECK(t.head_rows == 1);
    CHECK(t.attr("id") == "tbl:resp");
    REQUIRE(t.aligns.size() == 3);
    CHECK(t.aligns[0] == Align::Left);
    CHECK(t.aligns[1] == Align::Center);
    CHECK(t.aligns[2] == Align::Right);
    CHECK(t.caption.at(0).text == "Data responden");
}

TEST_CASE("citations and cross references") {
    auto doc = parse("Menurut @doe2020, hasilnya baik [@smith2019, hlm. 5; @lee2021]. Lihat @fig:a dan [-@kim2018].\n");
    auto& p = first(doc);
    std::vector<const Node*> refs;
    for (auto& c : p.children)
        if (c.type == NodeType::Citation || c.type == NodeType::CrossRef) refs.push_back(&c);
    REQUIRE(refs.size() == 4);
    CHECK(refs[0]->attr("keys") == "doe2020");
    CHECK(refs[0]->attr("mode") == "text");
    CHECK(refs[1]->attr("keys") == "smith2019,lee2021");
    CHECK(refs[1]->attr("mode") == "paren");
    CHECK(refs[1]->attr("locator") == "hlm. 5");
    CHECK(refs[2]->type == NodeType::CrossRef);
    CHECK(refs[2]->attr("id") == "fig:a");
    CHECK(refs[3]->attr("mode") == "year");
}

TEST_CASE("email addresses and code are not citations") {
    auto doc = parse("Hubungi ani@kampus.ac.id atau jalankan `git log @foo`.\n");
    for (auto& c : first(doc).children) CHECK(c.type != NodeType::Citation);
}

TEST_CASE("heading tags: parts, lists, bibliography") {
    auto doc = parse("# Kata Pengantar {pembuka -}\n\n# Daftar Gambar {pembuka daftar gambar}\n\n"
                     "# Daftar Isi {list contents}\n\n# Referensi {pustaka IEEEStyle}\n\n# Sampul {cover}\n");
    auto& k = doc.root.children;
    REQUIRE(k.size() == 5);
    CHECK(plain_text(k[0]) == "Kata Pengantar");
    CHECK(k[0].tags == std::vector<std::string>{"pembuka"});
    CHECK(k[0].has_class("unnumbered"));
    CHECK(k[1].attr("list") == "gambar");
    CHECK(k[1].tags == std::vector<std::string>{"pembuka"});
    CHECK(k[2].attr("list") == "contents");
    CHECK(k[3].attr("role") == "bibliography");
    CHECK(k[3].tags == std::vector<std::string>{"IEEEStyle"});
    CHECK(k[4].attr("role") == "cover");
}

TEST_CASE("escaped and custom tag delimiters") {
    auto doc = parse("# Rute \\{home\\}\n");
    CHECK(plain_text(first(doc)) == "Rute {home}");
    CHECK(first(doc).tags.empty());

    Diagnostics d;
    ParseOptions opts;
    opts.syntax = {"[[", "]]"};
    auto doc2 = parse_document("# Praktikum {home} [[lampiran]]\n\n[[halaman-baru]]\n", d, "t.md", opts);
    REQUIRE(doc2.root.children.size() == 2);
    CHECK(plain_text(doc2.root.children[0]) == "Praktikum {home}");
    CHECK(doc2.root.children[0].tags == std::vector<std::string>{"lampiran"});
    CHECK(doc2.root.children[1].type == NodeType::PageBreak);
}

TEST_CASE("format sections need the name at both ends") {
    auto doc = parse("Awal.\n\n::: Landscape\n| a |\n|---|\n| 1 |\n\n::: DuaKolom\nIsi.\n::: DuaKolom\n::: Landscape\n\n"
                     "{halaman-baru}\n\n```md\n::: bukan-section\n```\n");
    auto& k = doc.root.children;
    REQUIRE(k.size() == 4);
    CHECK(k[1].type == NodeType::Div);
    CHECK(k[1].attr("class") == "Landscape");
    REQUIRE(k[1].children.size() == 2);
    CHECK(k[1].children[0].type == NodeType::Table);
    CHECK(k[1].children[1].attr("class") == "DuaKolom");
    CHECK(k[2].type == NodeType::PageBreak);
    CHECK(k[3].text == "::: bukan-section");
}

TEST_CASE("section errors have line numbers") {
    Diagnostics d;
    parse_document("# A\n\n::: Landscape\nteks\n", d, "t.md");
    REQUIRE(d.size() == 1);
    CHECK(d[0].level == Diagnostic::Level::Error);
    CHECK(d[0].line == 3);

    Diagnostics d2;
    parse_document("teks\n\n:::\n", d2, "t.md");
    REQUIRE(d2.size() == 1);
    CHECK(d2[0].line == 3);
}

TEST_CASE("blocks remember their source line") {
    auto doc = parse("---\ntitle: x\n---\n\n# Judul\n\nParagraf satu\nlanjut.\n\n::: Landscape\n\nDalam section.\n::: Landscape\n");
    auto& k = doc.root.children;
    REQUIRE(k.size() == 3);
    CHECK(k[0].line == 5);
    CHECK(k[1].line == 7);
    CHECK(k[2].line == 10);
    CHECK(k[2].children.at(0).line == 12);
}

TEST_CASE("math fence with caption") {
    auto doc = parse("```math {#eq:x caption=\"Skor akhir\"}\na &= b \\\\\nc &= d\n```\n");
    auto& m = first(doc);
    REQUIRE(m.type == NodeType::DisplayMath);
    CHECK(m.attr("id") == "eq:x");
    CHECK(m.attr("caption") == "Skor akhir");
    CHECK(m.text == "a &= b \\\\\nc &= d");
}

TEST_CASE("display math with a label") {
    auto doc = parse("$$\nE = mc^2\n$$ {#eq:energi}\n");
    auto& m = first(doc);
    REQUIRE(m.type == NodeType::DisplayMath);
    CHECK(m.attr("id") == "eq:energi");
}

TEST_CASE("latex fence is passed through") {
    auto doc = parse("```latex\n\\vspace{1cm}\n```\n");
    CHECK(first(doc).type == NodeType::RawLatex);
    CHECK(first(doc).text == "\\vspace{1cm}");
}
