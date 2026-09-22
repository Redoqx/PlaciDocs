#include <doctest.h>

#include "core/pipeline.hpp"
#include "core/rules/rule_engine.hpp"
#include "core/style/style_loader.hpp"

using namespace placi;

namespace {
std::string table_md(int cols, int rows) {
    std::string md = "|";
    for (int c = 0; c < cols; ++c) md += " H" + std::to_string(c) + " |";
    md += "\n|";
    for (int c = 0; c < cols; ++c) md += "---|";
    md += "\n";
    for (int r = 0; r < rows; ++r) {
        md += "|";
        for (int c = 0; c < cols; ++c) md += " x |";
        md += "\n";
    }
    return md + "\n";
}

Style rules_style() {
    Diagnostics d;
    return load_style_string(R"(
rules:
  - when: { element: table, columns: { gt: 6 } }
    set: { page.orientation: landscape }
  - when: { element: table, rows: { gte: 30 } }
    set: { table.longtable: true }
  - when: { element: table, in: lampiran }
    set: { table.font_size: 9pt }
  - when: { element: heading, level: 2, class: { ne: unnumbered } }
    set: { page.break_before: true }
)", "r.yaml", d);
}
}  // namespace

TEST_CASE("wide tables become landscape, narrow ones stay portrait") {
    Diagnostics d;
    auto doc = parse_document(table_md(7, 3) + table_md(5, 3), d);
    apply_rules(doc.root, rules_style());
    REQUIRE(doc.root.children.size() == 2);
    CHECK(doc.root.children[0].prop("page.orientation") == "landscape");
    CHECK(doc.root.children[1].prop("page.orientation") == "");
}

TEST_CASE("row count and heading-tag scope conditions") {
    Diagnostics d;
    auto doc = parse_document(table_md(3, 30) + "# Data {appendix}\n\n" + table_md(3, 2) + "# Penutup\n\n" + table_md(3, 2), d);
    apply_rules(doc.root, rules_style());
    auto& k = doc.root.children;
    REQUIRE(k.size() == 5);
    CHECK(k[0].prop("table.longtable") == "true");
    CHECK(k[0].prop("table.font_size") == "");
    CHECK(k[2].prop("table.font_size") == "9pt");  // inside the {appendix} (alias of lampiran) scope
    CHECK(k[4].prop("table.font_size") == "");     // the scope ended at the next h1
}

TEST_CASE("class conditions and explicit landscape class") {
    Diagnostics d;
    auto doc = parse_document("## A\n\n## B {-}\n\n: Lebar {.landscape}\n\n" + table_md(2, 1), d);
    apply_rules(doc.root, rules_style());
    CHECK(doc.root.children[0].prop("page.break_before") == "true");
    CHECK(doc.root.children[1].prop("page.break_before") == "");
    CHECK(doc.root.children[2].prop("page.orientation") == "landscape");
}
