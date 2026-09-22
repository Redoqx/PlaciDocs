#include "core/ast.hpp"

#include <algorithm>

namespace placi {

bool Node::has_class(std::string_view c) const {
    return std::find(classes.begin(), classes.end(), c) != classes.end();
}

std::string Node::attr(const std::string& key, const std::string& fallback) const {
    auto it = attrs.find(key);
    return it == attrs.end() ? fallback : it->second;
}

std::string Node::prop(const std::string& key, const std::string& fallback) const {
    auto it = props.find(key);
    return it == props.end() ? fallback : it->second;
}

int Node::column_count() const {
    if (!aligns.empty()) return static_cast<int>(aligns.size());
    int n = 0;
    for (const auto& row : children) n = std::max(n, static_cast<int>(row.children.size()));
    return n;
}

int Node::body_row_count() const {
    return std::max(0, static_cast<int>(children.size()) - head_rows);
}

const char* to_string(NodeType t) {
    switch (t) {
        case NodeType::Document: return "Document";
        case NodeType::Heading: return "Heading";
        case NodeType::Paragraph: return "Paragraph";
        case NodeType::BlockQuote: return "BlockQuote";
        case NodeType::BulletList: return "BulletList";
        case NodeType::OrderedList: return "OrderedList";
        case NodeType::ListItem: return "ListItem";
        case NodeType::CodeBlock: return "CodeBlock";
        case NodeType::RawLatex: return "RawLatex";
        case NodeType::HorizontalRule: return "HorizontalRule";
        case NodeType::Table: return "Table";
        case NodeType::TableRow: return "TableRow";
        case NodeType::TableCell: return "TableCell";
        case NodeType::Figure: return "Figure";
        case NodeType::Div: return "Div";
        case NodeType::DivMarker: return "DivMarker";
        case NodeType::PageBreak: return "PageBreak";
        case NodeType::DisplayMath: return "DisplayMath";
        case NodeType::Text: return "Text";
        case NodeType::SoftBreak: return "SoftBreak";
        case NodeType::LineBreak: return "LineBreak";
        case NodeType::Emph: return "Emph";
        case NodeType::Strong: return "Strong";
        case NodeType::Strike: return "Strike";
        case NodeType::Underline: return "Underline";
        case NodeType::Code: return "Code";
        case NodeType::Link: return "Link";
        case NodeType::Image: return "Image";
        case NodeType::Math: return "Math";
        case NodeType::Citation: return "Citation";
        case NodeType::CrossRef: return "CrossRef";
    }
    return "?";
}

bool is_inline(NodeType t) {
    switch (t) {
        case NodeType::Text: case NodeType::SoftBreak: case NodeType::LineBreak:
        case NodeType::Emph: case NodeType::Strong: case NodeType::Strike:
        case NodeType::Underline: case NodeType::Code: case NodeType::Link:
        case NodeType::Image: case NodeType::Math: case NodeType::Citation:
        case NodeType::CrossRef:
            return true;
        default:
            return false;
    }
}

static void plain_text_into(const Node& n, std::string& out) {
    switch (n.type) {
        case NodeType::Text: case NodeType::Code: case NodeType::Math:
            out += n.text;
            return;
        case NodeType::SoftBreak: case NodeType::LineBreak:
            out += ' ';
            return;
        default:
            for (const auto& c : n.children) plain_text_into(c, out);
    }
}

std::string plain_text(const Node& n) {
    std::string out;
    plain_text_into(n, out);
    return out;
}

std::string dump(const Node& n, int indent) {
    std::string out(static_cast<size_t>(indent) * 2, ' ');
    out += to_string(n.type);
    if (n.level) out += " level=" + std::to_string(n.level);
    if (n.type == NodeType::Table)
        out += " cols=" + std::to_string(n.column_count()) + " rows=" + std::to_string(n.body_row_count());
    for (const auto& [k, v] : n.attrs) out += " " + k + "=\"" + v + "\"";
    for (const auto& c : n.classes) out += " ." + c;
    for (const auto& t : n.tags) out += " {" + t + "}";
    for (const auto& [k, v] : n.props) out += " [" + k + "=" + v + "]";
    if (!n.text.empty()) {
        std::string t = n.text;
        std::replace(t.begin(), t.end(), '\n', ' ');
        out += " \"" + t + "\"";
    }
    out += '\n';
    if (!n.caption.empty()) {
        out += std::string(static_cast<size_t>(indent + 1) * 2, ' ') + "caption:\n";
        for (const auto& c : n.caption) out += dump(c, indent + 2);
    }
    for (const auto& c : n.children) out += dump(c, indent + 1);
    return out;
}

}  // namespace placi
