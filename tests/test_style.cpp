#include <doctest.h>

#include "core/style/style_loader.hpp"

using namespace placi;

TEST_CASE("style values are loaded onto defaults") {
    Diagnostics d;
    auto st = load_style_string(R"(
name: Uji
page: { size: F4, margin: { top: 4cm } }
font: { family: Times New Roman, size: 12pt }
paragraph: { line_spacing: 2, indent: 1cm }
headings:
  all: { bold: false }
  h1: { label: "BAB {num}", numbering: "{Roman}", uppercase: true }
)", "uji.yaml", d);
    CHECK(d.empty());
    CHECK(st.name == "Uji");
    CHECK(st.page.size == "F4");
    CHECK(st.page.margin.top == "4cm");
    CHECK(st.page.margin.left == "3cm");  // untouched default
    CHECK(st.paragraph.line_spacing == 2.0);
    CHECK(st.headings[0].label == "BAB {num}");
    CHECK(st.headings[0].uppercase);
    CHECK_FALSE(st.headings[0].bold);
    CHECK_FALSE(st.headings[3].bold);
}

TEST_CASE("unknown keys warn with line numbers") {
    Diagnostics d;
    load_style_string("page:\n  size: A4\n  colour: red\n", "s.yaml", d);
    REQUIRE(d.size() == 1);
    CHECK(d[0].line == 3);
    CHECK(d[0].message.find("page.colour") != std::string::npos);
}

TEST_CASE("invalid values are errors with line numbers") {
    Diagnostics d;
    try {
        load_style_string("font:\n  size: besar\n", "s.yaml", d);
        FAIL("expected an error");
    } catch (const PlaciError& e) {
        CHECK(e.line == 2);
        CHECK(std::string(e.what()).find("font.size") != std::string::npos);
    }
    CHECK_THROWS_AS(load_style_string("page: { size: A3 }\n", "s.yaml", d), PlaciError);
    CHECK_THROWS_AS(load_style_string("page: [1, 2\n", "s.yaml", d), PlaciError);
}

TEST_CASE("rules are parsed") {
    Diagnostics d;
    auto st = load_style_string(R"(
rules:
  - when: { element: table, columns: { gt: 6 } }
    set: { page.orientation: landscape }
  - when: { element: heading, level: 1 }
    set: { page: { break_before: true } }
)", "r.yaml", d);
    REQUIRE(st.rules.size() == 2);
    CHECK(st.rules[0].element == "table");
    REQUIRE(st.rules[0].when.size() == 1);
    CHECK(st.rules[0].when[0].attr == "columns");
    CHECK(st.rules[0].when[0].op == CmpOp::Gt);
    CHECK(st.rules[0].when[0].value == "6");
    CHECK(st.rules[1].when[0].op == CmpOp::Eq);
    CHECK(st.rules[1].set[0].first == "page.break_before");
    CHECK_THROWS_AS(load_style_string("rules:\n  - when: { element: table }\n    set: { page.orientaton: x }\n", "r.yaml", d), PlaciError);
}

TEST_CASE("extends loads the base style first") {
    Diagnostics d;
    std::vector<fs::path> dirs{fs::path(PLACI_SOURCE_DIR) / "styles"};
    auto st = load_style(fs::path(PLACI_SOURCE_DIR) / "styles" / "laporan-kantor.yaml", d, dirs);
    CHECK(d.empty());
    CHECK(st.name == "Laporan Kantor");
    CHECK(st.font.family == "Arial");
    CHECK(st.page.size == "A4");                        // from default.yaml
    CHECK(st.headings[0].label == "{num}");            // overridden
    CHECK(st.rules.size() == 1);
}

TEST_CASE("built-in styles are valid") {
    std::vector<fs::path> dirs{fs::path(PLACI_SOURCE_DIR) / "styles"};
    for (auto& e : fs::directory_iterator(dirs[0])) {
        Diagnostics d;
        CAPTURE(e.path().string());
        CHECK_NOTHROW(load_style(e.path(), d, dirs));
        CHECK(d.empty());
    }
}
