#pragma once
// Strongly typed writing rules ("style"). The YAML loader fills this struct;
// the rest of the engine never sees YAML, so the file format can be swapped
// (e.g. for a PlaciDocs-specific format) by writing another loader.

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace placi {

struct Margins {
    std::string top = "3cm", bottom = "3cm", left = "3cm", right = "3cm";
};

struct PageStyle {
    std::string size = "A4";  // A4, A5, B5, letter, legal, F4
    Margins margin;
    bool twoside = false;
};

struct FontStyle {
    std::string family;       // main font; empty = Latin Modern (LaTeX default)
    std::string sans;
    std::string mono;
    std::string size = "12pt";
};

struct ParagraphStyle {
    double line_spacing = 1.5;          // 1 = single, 1.5 = "spasi 1.5", 2 = double
    std::string indent = "1.25cm";      // first-line indent
    bool indent_first = true;           // indent the first paragraph after a heading
    std::string space_before = "0pt";
    std::string space_after = "0pt";
    std::string align = "justify";      // justify | left | right | center
};

struct HeadingStyle {
    bool numbered = true;
    std::string numbering = "{n}";      // number format: {n} {roman} {Roman} {alpha} {Alpha} {h1}..{h5}
    std::string label = "{num}";        // how the number is displayed, e.g. "BAB {num}"
    std::string label_position = "inline";  // inline | above (label on its own line)
    std::string label_sep = "1em";      // gap between inline label and title
    std::string align = "left";         // left | center | right
    std::string size;                   // e.g. 14pt; empty = body size
    bool bold = true;
    bool italic = false;
    bool uppercase = false;
    bool new_page = false;
    std::string space_before = "12pt";
    std::string space_after = "6pt";
    bool in_toc = true;
};

struct PageNumberStyle {
    std::string style = "arabic";       // arabic | roman | Roman | alpha | Alpha | none
    std::string position = "bottom-center";  // (top|bottom)-(left|center|right) | none
    std::string first_page;             // position on a chapter's first page; empty = same

    std::string first() const { return first_page.empty() ? position : first_page; }
    bool operator==(const PageNumberStyle& o) const {
        return style == o.style && position == o.position && first() == o.first();
    }
};

// Look of captions; the words and numbers come from the list (see ListStyle).
struct CaptionStyle {
    std::string position;               // above | below
    std::string align = "center";       // center | left | justify
    std::string separator = ". ";
    bool label_bold = true;
    std::string font_size;              // empty = body size
};

struct FigureStyle {
    CaptionStyle caption;
    std::string placement = "H";        // H = exactly here; or LaTeX float spec like "htbp"
    std::string width = "80%";          // default image width

    FigureStyle() { caption.position = "below"; }
};

struct TableStyle {
    CaptionStyle caption;
    std::string placement = "H";
    std::string font_size;              // empty = body size
    std::string borders = "grid";       // grid | booktabs | horizontal | none
    bool header_bold = true;
    std::string header_background;      // e.g. "gray!20"
    std::string width = "auto";         // auto | full
    double line_spacing = 1.0;

    TableStyle() { caption.position = "above"; }
};

struct BibliographyStyle {
    std::string style = "apalike";      // any .bst known to Tectonic (apalike, plainnat, ieeetr, ...)
    std::string citation = "author-year";  // author-year | numeric
    std::string title = "Daftar Pustaka";  // used only when no heading carries the `pustaka` tag
    bool in_toc = true;
};

struct ContentsStyle {
    int depth = 3;                      // deepest heading level listed in the TOC
};

struct CrossRefStyle {
    bool prefix = true;                 // @fig:x -> "Gambar 1.1" (false -> "1.1")
};

// Delimiters of heading tags / attribute blocks: "# Judul {pembuka -}".
struct SyntaxStyle {
    std::string tag_open = "{";
    std::string tag_close = "}";
};

// A numbered kind of item with its own list ("Daftar Gambar", "Daftar Rumus", ...).
struct ListStyle {
    std::string key;                    // gambar
    std::vector<std::string> aliases;   // figures
    std::string title;                  // default heading text ("DAFTAR GAMBAR")
    std::string prefix;                 // caption / reference word ("Gambar")
    std::string label;                  // label prefix that puts an item in this list ("fig")
    std::string numbering = "{h1}.{n}";
    std::string kind = "float";         // figure | table | equation | float
};

// Formatting attached to a heading by a tag: "# Kata Pengantar {pembuka}".
// Scoped fields apply to the heading and everything below it; the others only
// to the tagged heading's level.
struct TagStyle {
    std::string key;
    std::vector<std::string> aliases;
    // scoped
    std::optional<bool> numbered;
    std::optional<PageNumberStyle> page_numbering;
    // tagged level only
    std::optional<std::string> label;
    std::optional<std::string> numbering;
    std::optional<std::string> align;
    std::optional<bool> uppercase;
    std::optional<bool> new_page;
    std::optional<bool> in_toc;
    bool hide_title = false;
    std::string template_tex;           // raw LaTeX with {{meta}} placeholders
    // bibliography (when on the `pustaka` heading)
    std::string bibliography_style;
    std::string citation;
};

// Page setup of a "::: Name ... ::: Name" block (like a Word section).
struct SectionStyle {
    std::string key;
    std::vector<std::string> aliases;
    std::string orientation;            // "" (inherit) | portrait | landscape
    Margins margin{"", "", "", ""};     // empty = inherit
    int columns = 1;
    std::string font_size;
};

enum class CmpOp { Eq, Ne, Gt, Gte, Lt, Lte, Contains };

struct Condition {
    std::string attr;    // columns, rows, level, class, id, width, in, ...
    CmpOp op = CmpOp::Eq;
    std::string value;
};

struct Rule {
    std::string element;                 // table | figure | heading | paragraph | code | math | *
    std::vector<Condition> when;
    std::vector<std::pair<std::string, std::string>> set;  // "page.orientation" -> "landscape"
    std::string source;                  // "file:line" for diagnostics
};

struct Style {
    std::string name;
    std::string language = "indonesian";
    PageStyle page;
    FontStyle font;
    ParagraphStyle paragraph;
    std::array<HeadingStyle, 6> headings;
    PageNumberStyle page_numbering;
    FigureStyle figure;
    TableStyle table;
    BibliographyStyle bibliography;
    ContentsStyle contents;
    CrossRefStyle cross_reference;
    SyntaxStyle syntax;
    std::map<std::string, ListStyle> lists;
    std::map<std::string, TagStyle> tags;
    std::map<std::string, SectionStyle> sections;
    std::string cover;                   // raw LaTeX template with {{title}}, {{author}}, ...
    std::string preamble;                // extra raw LaTeX preamble
    std::vector<Rule> rules;
    std::string base_dir;                // directory of the style file (for cover assets)

    Style();

    // Lookup by key or alias.
    const ListStyle* find_list(const std::string& name) const;
    const ListStyle* list_for_label(const std::string& label_prefix) const;
    const ListStyle* list_of_kind(const std::string& kind) const;
    const TagStyle* find_tag(const std::string& name) const;
    const SectionStyle* find_section(const std::string& name) const;
};

// Property keys that a rule's `set:` may use (validated by the loader).
bool is_known_rule_property(const std::string& key);

// Built-in heading tag words (both languages).
bool is_builtin_tag(const std::string& word);

}  // namespace placi
