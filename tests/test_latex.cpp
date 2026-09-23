#include <doctest.h>

#include "core/latex/emitter.hpp"
#include "core/latex/escape.hpp"
#include "core/latex/preamble.hpp"
#include "core/pipeline.hpp"
#include "core/rules/rule_engine.hpp"
#include "core/style/style_loader.hpp"
#include "core/passes/passes.hpp"

using namespace placi;

namespace {
bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string body(std::string_view md, const Style& st = Style{}) {
    Diagnostics d;
    auto doc = parse_document(md, d);
    apply_rules(doc.root, st);
    return latex::emit_blocks(doc.root, st, d);
}
}  // namespace

TEST_CASE("special characters are escaped") {
    CHECK(latex::escape("100% & $5 #1 a_b {x} ~ ^ \\") ==
          "100\\% \\& \\$5 \\#1 a\\_b \\{x\\} \\textasciitilde{} \\^{} \\textbackslash{}");
}

TEST_CASE("numbering templates") {
    CHECK(latex::numbering_expr("{h1}.{n}", "section") == "\\placichapnum{}.\\arabic{section}");
    CHECK(latex::numbering_expr("{h1}.{h2}.{n}", "subsection") == "\\placichapnum{}.\\arabic{section}.\\arabic{subsection}");
    CHECK(latex::numbering_expr("{Roman}", "chapter") == "\\Roman{chapter}");
    CHECK(latex::numbering_expr("{alpha})", "subsection") == "\\alph{subsection})");
    CHECK(latex::numbering_expr("({h1}.{n})", "equation") == "(\\placichapnum{}.\\arabic{equation})");
    CHECK(latex::label_expr("BAB {num}", "\\thechapter") == "BAB \\thechapter");
}

TEST_CASE("inline markup") {
    auto tex = body("Ini **tebal**, *miring*, `kode_x`, ~~coret~~, $x^2$ dan [tautan](https://a.b/c%20d).\n");
    CHECK(has(tex, "\\textbf{tebal}"));
    CHECK(has(tex, "\\emph{miring}"));
    CHECK(has(tex, "\\texttt{kode\\_x}"));
    CHECK(has(tex, "\\sout{coret}"));
    CHECK(has(tex, "$x^2$"));
    CHECK(has(tex, "\\href{https://a.b/c\\%20d}{tautan}"));
}

TEST_CASE("headings, labels and unnumbered headings") {
    auto tex = body("# Pendahuluan {#sec:intro}\n\n# Kata Pengantar {-}\n\n## Latar\n");
    CHECK(has(tex, "\\chapter{Pendahuluan}\\label{sec:intro}"));
    CHECK(has(tex, "\\chapter*{Kata Pengantar}\\phantomsection\\addcontentsline{toc}{chapter}{Kata Pengantar}"));
    CHECK(has(tex, "\\section{Latar}"));
}

TEST_CASE("citations use natbib, cross references get prefixes") {
    Style st;
    auto tex = body("![Diagram](a.png){#fig:d}\n\nMenurut @doe2020 [lihat @fig:d; @x] dan [@doe2020, hlm. 3; @lee].\n", st);
    CHECK(has(tex, "\\citet{doe2020}"));
    CHECK(has(tex, "\\citep[hlm. 3]{doe2020,lee}"));
    tex = body("![Diagram](a.png){#fig:d}\n\nLihat @fig:d.\n", st);
    CHECK(has(tex, "Gambar~\\ref{fig:d}."));
}

TEST_CASE("references to unknown labels are reported") {
    Diagnostics d;
    auto doc = parse_document("![Diagram](a.png){#fig:d}\n\nLihat @fig:d dan @tbl:hilang.\n", d);
    latex::emit_blocks(doc.root, Style{}, d);
    REQUIRE(d.size() == 1);
    CHECK(d[0].message.find("tbl:hilang") != std::string::npos);
}

TEST_CASE("figures") {
    auto tex = body("![Arsitektur](img/a.png){#fig:a width=50%}\n");
    CHECK(has(tex, "\\begin{figure}[H]"));
    CHECK(has(tex, "\\includegraphics[width=0.5\\linewidth,height=0.75\\textheight,keepaspectratio]{img/a.png}"));
    CHECK(has(tex, "\\caption{Arsitektur}\\label{fig:a}"));
}

TEST_CASE("tables: grid borders, caption above, landscape grouping") {
    Diagnostics d;
    auto st = load_style_string("rules:\n  - when: { element: table, columns: { gt: 2 } }\n    set: { page.orientation: landscape }\n",
                                "r.yaml", d);
    auto tex = body("Tabel: Hasil {#tbl:h}\n\n| A | B |\n|---|--:|\n| 1 | 2 |\n\n| A | B | C |\n|---|---|---|\n| 1 | 2 | 3 |\n", st);
    CHECK(has(tex, "\\caption{Hasil}\\label{tbl:h}"));
    CHECK(has(tex, "\\begin{tabular}{|l|r|}"));
    CHECK(has(tex, "\\textbf{A} & \\textbf{B} \\\\"));
    CHECK(tex.find("\\caption{Hasil}") < tex.find("\\begin{tabular}{|l|r|}"));
    CHECK(tex.find("\\begin{landscape}") > tex.find("\\begin{tabular}{|l|r|}"));
    CHECK(has(tex, "\\begin{tabular}{|l|l|l|}"));
}

TEST_CASE("long cell content switches to paragraph columns") {
    std::string longcell(200, 'a');
    auto tex = body("| A | B |\n|---|---|\n| " + longcell + " | b |\n");
    CHECK(has(tex, "p{\\dimexpr"));
}

TEST_CASE("longtable rule") {
    Diagnostics d;
    auto st = load_style_string("rules:\n  - when: { element: table }\n    set: { table.longtable: true }\n", "r.yaml", d);
    auto tex = body("| A |\n|---|\n| 1 |\n", st);
    CHECK(has(tex, "\\begin{longtable}"));
    CHECK(has(tex, "\\endhead"));
}

Style skripsi() {
    Diagnostics d;
    std::vector<fs::path> dirs{fs::path(PLACI_SOURCE_DIR) / "styles"};
    return load_style(fs::path(PLACI_SOURCE_DIR) / "styles" / "skripsi-umum.yaml", d, dirs);
}

std::string document(std::string_view md, const Style& st, Diagnostics& d) {
    auto doc = parse_document(md, d, "t.md", ParseOptions::from_style(st));
    apply_rules(doc.root, st);
    return latex::emit_document(doc, st, {"references"}, d);
}

TEST_CASE("full document with skripsi style") {
    Diagnostics d;
    auto st = skripsi();
    auto tex = document("---\ntitle: Judul\nauthor: Ani\n---\n\n# Kata Pengantar {pembuka}\n\nTerima kasih.\n\n"
                        "# DAFTAR ISI {pembuka daftar isi}\n\n# Pendahuluan\n\nTeks [@a].\n\n# Kuesioner {lampiran}\n\n## Bagian\n",
                        st, d);
    CHECK(has(tex, "\\documentclass[12pt,oneside]{report}"));
    CHECK(has(tex, "top=4cm"));
    CHECK(has(tex, "\\renewcommand{\\thechapter}{\\Roman{chapter}}"));
    CHECK(has(tex, "\\MakeUppercase{BAB \\thechapter}"));
    CHECK(has(tex, "\\begin{titlepage}"));
    // front part: roman page numbers, unnumbered chapters that still appear in the TOC
    auto front = tex.find("\\pagenumbering{roman}");
    auto main = tex.find("\\pagenumbering{arabic}", front);
    REQUIRE(front != std::string::npos);
    REQUIRE(main != std::string::npos);
    CHECK(tex.find("\\chapter*{Kata Pengantar}\\phantomsection\\addcontentsline{toc}{chapter}{Kata Pengantar}") < main);
    CHECK(has(tex, "\\chapter*{DAFTAR ISI}\n\n\\placilistof{toc}"));
    CHECK(tex.find("\\chapter{Pendahuluan}") > main);
    // appendix tag: new label, letters, counter restarts, {h1} follows the letter
    CHECK(has(tex, "\\MakeUppercase{LAMPIRAN \\thechapter}"));
    CHECK(has(tex, "\\renewcommand{\\thechapter}{\\Alph{chapter}}\\renewcommand{\\placichapnum}{\\thechapter}"));
    CHECK(has(tex, "\\setcounter{chapter}{0}\\renewcommand{\\theHchapter}{placi1.\\arabic{chapter}}\n\\chapter{Kuesioner}"));
    CHECK(has(tex, "\\bibliography{references}"));
}

TEST_CASE("bibliography under a tagged heading with its own style") {
    Diagnostics d;
    auto tex = document("Teks [@a].\n\n# Referensi {pustaka IEEEStyle}\n", skripsi(), d);
    CHECK(has(tex, "\\usepackage[numbers,square]{natbib}"));
    CHECK(has(tex, "\\chapter*{Referensi}"));
    CHECK(has(tex, "\\renewcommand{\\bibsection}{}\n\\bibliographystyle{ieeetr}"));
    CHECK(tex.find("\\bibliography{references}") == tex.rfind("\\bibliography{references}"));  // only once
}

TEST_CASE("user lists get their own float, list and cross-reference prefix") {
    Diagnostics d;
    auto st = skripsi();
    std::map<std::string, std::string> meta{{"lists.grafik.prefix", "Grafik"}, {"lists.grafik.label", "gfk"},
                                            {"lists.grafik.title", "DAFTAR GRAFIK"}};
    apply_document_lists(st, meta, d);
    auto tex = document("# DAFTAR GRAFIK {pembuka daftar grafik}\n\n# Isi\n\n![Penjualan](a.png){#gfk:jual}\n\nLihat @gfk:jual.\n", st, d);
    CHECK(has(tex, "\\DeclareFloatingEnvironment[fileext=lopa,listname={DAFTAR GRAFIK},name={Grafik},placement=H]{placifloata}"));
    CHECK(has(tex, "\\begin{placifloata}[H]"));
    CHECK(has(tex, "\\placilistof{lopa}"));
    CHECK(has(tex, "Grafik~\\ref{gfk:jual}"));
}

TEST_CASE("captioned equations go to the equation list") {
    Diagnostics d;
    auto tex = document("# DAFTAR RUMUS {daftar rumus}\n\n# Isi\n\n```math {#eq:s caption=\"Skor\"}\na &= b \\\\\nc &= d\n```\n\nLihat @eq:s.\n",
                        skripsi(), d);
    CHECK(has(tex, "\\begin{equation}\\label{eq:s}\n\\begin{aligned}"));
    CHECK(has(tex, "\\addcontentsline{loe}{equation}{\\protect\\numberline{\\theequation}Skor}"));
    CHECK(has(tex, "\\placilistof{loe}"));
    CHECK(has(tex, "Rumus~\\ref{eq:s}"));
}

TEST_CASE("format sections: landscape, columns, margins") {
    Diagnostics d;
    auto st = skripsi();
    SectionStyle narrow;
    narrow.key = "Sempit";
    narrow.margin.left = "2cm";
    st.sections["Sempit"] = narrow;
    auto tex = document("::: JadiLandscape\nA\n::: JadiLandscape\n\n::: DuaKolom\n![G](a.png)\n::: DuaKolom\n\n::: Sempit\nB\n::: Sempit\n\n::: Apa\nC\n::: Apa\n",
                        st, d);
    CHECK(has(tex, "\\begin{landscape}\nA"));
    CHECK(has(tex, "\\begin{multicols}{2}"));
    CHECK(has(tex, "\\captionsetup{type=figure}"));  // no floats inside multicols
    CHECK(has(tex, "\\newgeometry{top=4cm,bottom=3cm,left=2cm,right=3cm,headheight=15pt}"));
    CHECK(has(tex, "\\restoregeometry"));
    bool warned = false;
    for (auto& x : d) warned |= x.message.find("unknown section 'Apa'") != std::string::npos;
    CHECK(warned);
}

TEST_CASE("template tags and page breaks") {
    Diagnostics d;
    auto tex = document("---\ntitle: Judul\nauthor: Ani\nid: 123\nadvisor: Budi\n---\n\n# Lembar Pengesahan {pengesahan}\n\n# Isi\n\nA\n\n{halaman-baru}\n\nB\n",
                        skripsi(), d);
    CHECK(has(tex, "Oleh: Ani (123)"));
    CHECK(has(tex, "Budi\\par"));
    CHECK(has(tex, "A\n\n\\clearpage\n\nB"));
}

TEST_CASE("LaTeX lines map back to the Markdown lines that produced them") {
    Diagnostics d;
    auto st = skripsi();
    // Lines:      1   2          3    4  5       6  7                8  9        10        11   12 13
    const char* md = R"MD(---
title: Uji
---

# Bab {#sec:a}

Paragraf satu.

```latex
\perintah
```

Paragraf akhir.
)MD";
    auto doc = parse_document(md, d, "t.md", ParseOptions::from_style(st));
    latex::LineMap map;
    auto tex = latex::emit_document(doc, st, {}, d, &map);
    REQUIRE(!map.empty());

    size_t raw_line = 0, last_line = 0, line = 1;
    for (size_t i = 0; i < tex.size(); ++i) {
        if (tex[i] == '\n') ++line;
        if (!raw_line && tex.compare(i, 9, "\\perintah") == 0) raw_line = line;
        if (tex.compare(i, 14, "Paragraf akhir") == 0) last_line = line;
    }
    REQUIRE(raw_line > 0);
    REQUIRE(last_line > 0);
    CHECK(latex::markdown_line_for(map, raw_line) == 10);   // the \perintah line
    CHECK(latex::markdown_line_for(map, last_line) == 13);  // the closing paragraph
    CHECK(latex::markdown_line_for(map, 1) == 0);           // still inside the preamble
}
