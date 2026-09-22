#pragma once
// PlaciDocs document model.
//
// Every stage of the pipeline (parser -> passes -> rule engine -> emitter)
// works on this tree. It is deliberately independent of md4c and of LaTeX so
// that either end can be replaced (custom parser, live preview renderer, ...).

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace placi {

enum class NodeType {
    // blocks
    Document,
    Heading,        // level = 1..6; tags; attrs["list"] (daftar X), attrs["role"] (bibliography|cover)
    Paragraph,
    BlockQuote,
    BulletList,
    OrderedList,    // attrs["start"]
    ListItem,
    CodeBlock,      // text = code, attrs["lang"]
    RawLatex,       // text = latex (```latex fence or front-end generated)
    HorizontalRule,
    Table,          // children = TableRow; head_rows, aligns
    TableRow,
    TableCell,
    Figure,         // attrs["src"], ["width"], ["id"]; caption = inline nodes
    Div,           // attrs["class"] = name of the ::: block
    DivMarker,      // transient: produced by the parser, folded into Div by passes
    PageBreak,
    DisplayMath,    // text = latex; attrs["id"] (numbered), attrs["caption"] (listed)
    // inlines
    Text,
    SoftBreak,
    LineBreak,
    Emph,
    Strong,
    Strike,
    Underline,
    Code,           // text
    Link,           // attrs["href"]
    Image,          // attrs["src"], children = alt text (the caption)
    Math,           // text = latex
    Citation,       // attrs["keys"] = "a,b", ["mode"] = paren|text|year, ["locator"]
    CrossRef,       // attrs["id"] = "fig:x"
};

enum class Align { Default, Left, Center, Right };

struct Node {
    NodeType type = NodeType::Text;
    int level = 0;                             // heading level
    size_t line = 0;                           // 1-based source line (0 = unknown)
    std::string text;                          // literal payload (Text, Code, Math, ...)
    std::map<std::string, std::string> attrs;  // user/semantic attributes ({#id .class key=val})
    std::map<std::string, std::string> props;  // layout properties set by the rule engine
    std::vector<std::string> classes;
    std::vector<std::string> tags;             // heading tags from the style: {pembuka IEEEStyle}
    std::vector<Node> children;
    std::vector<Node> caption;                 // inline content, for Figure and Table

    // tables
    int head_rows = 0;
    std::vector<Align> aligns;

    Node() = default;
    explicit Node(NodeType t) : type(t) {}
    Node(NodeType t, std::string txt) : type(t), text(std::move(txt)) {}

    bool has_class(std::string_view c) const;
    std::string attr(const std::string& key, const std::string& fallback = {}) const;
    std::string prop(const std::string& key, const std::string& fallback = {}) const;

    // table helpers
    int column_count() const;
    int body_row_count() const;
};

struct Document {
    Node root{NodeType::Document};
    std::map<std::string, std::string> meta;   // front matter (flat: title, author, style, ...)
};

const char* to_string(NodeType t);
bool is_inline(NodeType t);

// Concatenated plain text of a subtree (used for heuristics and captions).
std::string plain_text(const Node& n);

// Indented, human readable dump (used by `placi ast` and tests).
std::string dump(const Node& n, int indent = 0);

}  // namespace placi
