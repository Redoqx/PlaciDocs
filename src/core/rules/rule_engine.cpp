#include "core/rules/rule_engine.hpp"

#include <cctype>
#include <vector>

#include "core/util.hpp"

namespace placi {

std::string element_name(NodeType t) {
    switch (t) {
        case NodeType::Table: return "table";
        case NodeType::Figure: return "figure";
        case NodeType::Heading: return "heading";
        case NodeType::Paragraph: return "paragraph";
        case NodeType::CodeBlock: return "code";
        case NodeType::DisplayMath: return "math";
        case NodeType::BulletList: case NodeType::OrderedList: return "list";
        case NodeType::BlockQuote: return "quote";
        default: return {};
    }
}

namespace {

size_t count_words(std::string_view s) {
    size_t n = 0;
    bool in = false;
    for (char c : s) {
        bool ws = std::isspace(static_cast<unsigned char>(c));
        if (!ws && !in) ++n;
        in = !ws;
    }
    return n;
}

std::optional<double> as_number(std::string_view s) {
    s = trim(s);
    if (!s.empty() && s.back() == '%') s.remove_suffix(1);
    if (s.empty()) return std::nullopt;
    try {
        size_t used = 0;
        double d = std::stod(std::string(s), &used);
        if (used != s.size()) return std::nullopt;
        return d;
    } catch (...) {
        return std::nullopt;
    }
}

bool compare(const std::string& actual, CmpOp op, const std::string& expected) {
    auto a = as_number(actual), e = as_number(expected);
    if (a && e) {
        switch (op) {
            case CmpOp::Eq: return *a == *e;
            case CmpOp::Ne: return *a != *e;
            case CmpOp::Gt: return *a > *e;
            case CmpOp::Gte: return *a >= *e;
            case CmpOp::Lt: return *a < *e;
            case CmpOp::Lte: return *a <= *e;
            case CmpOp::Contains: break;
        }
    }
    switch (op) {
        case CmpOp::Eq: return actual == expected;
        case CmpOp::Ne: return actual != expected;
        case CmpOp::Contains: return actual.find(expected) != std::string::npos;
        default: return false;  // ordering on non-numbers never matches
    }
}

// Tests one condition; classes and enclosing divs are sets, not single values.
bool matches(const Node& n, const std::map<std::string, std::string>& m, const std::vector<std::string>& enclosing,
             const Condition& c) {
    if (c.attr == "class" || c.attr == "in") {
        const auto& set = c.attr == "class" ? n.classes : enclosing;
        bool found = false;
        for (auto& v : set) found |= (c.op == CmpOp::Contains ? v.find(c.value) != std::string::npos : v == c.value);
        return c.op == CmpOp::Ne ? !found : found;
    }
    auto it = m.find(c.attr);
    std::string actual = it == m.end() ? std::string() : it->second;
    return compare(actual, c.op, c.value);
}

// Walks the tree keeping track of what encloses each node: the names of
// "::: Name" sections and the tags of the heading scopes it sits in.
struct Walker {
    const Style& style;
    std::vector<std::string> sections;
    std::vector<std::pair<int, std::vector<std::string>>> scopes;  // heading level, tags

    std::vector<std::string> enclosing() const {
        std::vector<std::string> all = sections;
        for (auto& [lvl, tags] : scopes) all.insert(all.end(), tags.begin(), tags.end());
        return all;
    }

    void walk(Node& n) {
        if (n.type == NodeType::Heading) {
            while (!scopes.empty() && scopes.back().first >= n.level) scopes.pop_back();
            std::vector<std::string> tags;
            for (auto& t : n.tags)
                if (auto* ts = style.find_tag(t)) tags.push_back(ts->key), tags.insert(tags.end(), ts->aliases.begin(), ts->aliases.end());
                else tags.push_back(t);
            scopes.emplace_back(n.level, std::move(tags));
        }
        std::string el = element_name(n.type);
        if (!el.empty()) {
            // Explicit author intent: {.landscape} on any block.
            if (n.has_class("landscape")) n.props["page.orientation"] = "landscape";
            auto metrics = node_metrics(n);
            auto around = enclosing();
            for (const auto& rule : style.rules) {
                if (rule.element != "*" && rule.element != el) continue;
                bool ok = true;
                for (const auto& c : rule.when) ok = ok && matches(n, metrics, around, c);
                if (!ok) continue;
                for (const auto& [k, v] : rule.set) n.props[k] = v;
            }
        }
        const size_t before = sections.size();
        if (n.type == NodeType::Div) {
            // A section is known by the name used in the document, its key and its aliases.
            sections.push_back(n.attr("class"));
            if (auto* s = style.find_section(n.attr("class"))) {
                sections.push_back(s->key);
                sections.insert(sections.end(), s->aliases.begin(), s->aliases.end());
            }
        }
        for (auto& c : n.children) walk(c);
        sections.resize(before);
    }
};

}  // namespace

std::map<std::string, std::string> node_metrics(const Node& n) {
    std::map<std::string, std::string> m;
    m["id"] = n.attr("id");
    switch (n.type) {
        case NodeType::Table: {
            m["columns"] = std::to_string(n.column_count());
            m["rows"] = std::to_string(n.body_row_count());
            size_t longest = 0;
            for (auto& row : n.children)
                for (auto& cell : row.children) longest = std::max(longest, plain_text(cell).size());
            m["longest_cell"] = std::to_string(longest);
            break;
        }
        case NodeType::Figure:
            m["width"] = n.attr("width");
            m["src"] = n.attr("src");
            break;
        case NodeType::Heading:
            m["level"] = std::to_string(n.level);
            m["numbered"] = n.has_class("unnumbered") ? "false" : "true";
            break;
        case NodeType::Paragraph: {
            auto t = plain_text(n);
            m["length"] = std::to_string(t.size());
            m["words"] = std::to_string(count_words(t));
            break;
        }
        case NodeType::CodeBlock: {
            size_t lines = n.text.empty() ? 0 : 1;
            for (char c : n.text) lines += c == '\n';
            m["lines"] = std::to_string(lines);
            m["lang"] = n.attr("lang");
            break;
        }
        case NodeType::BulletList:
        case NodeType::OrderedList:
            m["items"] = std::to_string(n.children.size());
            break;
        default:
            break;
    }
    return m;
}

void apply_rules(Node& root, const Style& style) {
    Walker w{style, {}, {}};
    w.walk(root);
}

}  // namespace placi
