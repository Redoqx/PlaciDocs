#include "core/parser/md_parser.hpp"

#include <md4c.h>

#include <algorithm>
#include <cstdint>
#include <string>

#include "core/parser/preprocess.hpp"
#include "core/style/style_loader.hpp"

namespace placi {

namespace {

std::string attr_text(const MD_ATTRIBUTE& a) {
    return a.text ? std::string(a.text, a.size) : std::string();
}

void append_utf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::string decode_entity(std::string_view e) {
    if (e.size() > 3 && e[1] == '#') {
        uint32_t cp = 0;
        try {
            bool hex = e[2] == 'x' || e[2] == 'X';
            cp = static_cast<uint32_t>(std::stoul(std::string(e.substr(hex ? 3 : 2, e.size() - (hex ? 4 : 3))), nullptr, hex ? 16 : 10));
        } catch (...) {
            return std::string(e);
        }
        std::string out;
        append_utf8(out, cp);
        return out;
    }
    struct { std::string_view name, value; } static constexpr kNamed[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"},
        {"&nbsp;", "\xC2\xA0"}, {"&copy;", "\xC2\xA9"}, {"&reg;", "\xC2\xAE"}, {"&deg;", "\xC2\xB0"},
        {"&ndash;", "\xE2\x80\x93"}, {"&mdash;", "\xE2\x80\x94"}, {"&hellip;", "\xE2\x80\xA6"},
    };
    for (auto& n : kNamed)
        if (e == n.name) return std::string(n.value);
    return std::string(e);
}

struct Builder {
    Document doc;
    std::vector<Node*> stack;
    // Source line tracking: md4c hands out pointers into the buffer it parses.
    const char* base = nullptr;
    size_t size = 0;
    std::vector<size_t> line_starts;        // offsets of each markdown line
    const std::vector<size_t>* line_map = nullptr;

    size_t source_line(const char* p) const {
        if (!base || p < base || p >= base + size) return 0;
        size_t off = static_cast<size_t>(p - base);
        auto it = std::upper_bound(line_starts.begin(), line_starts.end(), off);
        size_t idx = static_cast<size_t>(it - line_starts.begin()) - 1;
        return idx < line_map->size() ? (*line_map)[idx] : 0;
    }
    // The first text seen inside a block tells us where the block starts.
    void mark_line(const char* p) {
        size_t line = 0;
        for (size_t i = stack.size(); i-- > 1;) {
            if (stack[i]->line) break;
            if (!line) line = source_line(p);
            if (!line) return;
            stack[i]->line = line;
        }
    }
    int html_depth = 0;  // > 0 while inside a raw HTML block (dropped)
    bool in_thead = false;

    Node& top() { return *stack.back(); }

    Node& push(Node n) {
        top().children.push_back(std::move(n));
        Node* p = &top().children.back();
        stack.push_back(p);
        return *p;
    }
    void pop() { stack.pop_back(); }

    void add_text(std::string_view s) {
        auto& kids = top().children;
        if (!kids.empty() && kids.back().type == NodeType::Text) {
            kids.back().text += s;
        } else {
            kids.emplace_back(NodeType::Text, std::string(s));
        }
    }

    // A paragraph consisting only of a preprocess sentinel becomes a DivMarker:
    //   "open Name|12"  "close Name|20"  "pagebreak|7"
    void finish_paragraph(Node& p) {
        if (p.children.size() != 1 || p.children[0].type != NodeType::Text) return;
        std::string_view t = p.children[0].text;
        if (t.size() < 2 * kSentinel.size() || t.substr(0, kSentinel.size()) != kSentinel) return;
        t = t.substr(kSentinel.size(), t.size() - 2 * kSentinel.size());
        Node m(NodeType::DivMarker);
        size_t bar = t.rfind('|');
        if (bar != std::string_view::npos) {
            try { m.line = std::stoul(std::string(t.substr(bar + 1))); } catch (...) {}
            t = t.substr(0, bar);
        }
        size_t sp = t.find(' ');
        m.attrs["kind"] = std::string(t.substr(0, sp));
        if (sp != std::string_view::npos) m.attrs["class"] = std::string(t.substr(sp + 1));
        p = std::move(m);
    }
};

Builder& B(void* ud) { return *static_cast<Builder*>(ud); }

int enter_block(MD_BLOCKTYPE type, void* detail, void* ud) {
    auto& b = B(ud);
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: b.push(Node(NodeType::BlockQuote)); break;
        case MD_BLOCK_UL: b.push(Node(NodeType::BulletList)); break;
        case MD_BLOCK_OL: {
            auto& n = b.push(Node(NodeType::OrderedList));
            n.attrs["start"] = std::to_string(static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start);
            break;
        }
        case MD_BLOCK_LI: b.push(Node(NodeType::ListItem)); break;
        case MD_BLOCK_HR: b.push(Node(NodeType::HorizontalRule)); break;
        case MD_BLOCK_H: {
            auto& n = b.push(Node(NodeType::Heading));
            n.level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
            break;
        }
        case MD_BLOCK_CODE: {
            auto* d = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            std::string lang = attr_text(d->lang);
            NodeType t = lang == "latex" || lang == "tex" ? NodeType::RawLatex
                       : lang == "math" ? NodeType::DisplayMath : NodeType::CodeBlock;
            auto& n = b.push(Node(t));
            if (!lang.empty()) n.attrs["lang"] = lang;
            if (t == NodeType::DisplayMath) {
                // ```math {#eq:x caption="..."} -> the attribute block is resolved by the passes
                std::string info = attr_text(d->info);
                n.attrs["info"] = std::string(trim(std::string_view(info).substr(lang.size())));
            }
            break;
        }
        case MD_BLOCK_HTML: b.html_depth++; break;
        case MD_BLOCK_P: b.push(Node(NodeType::Paragraph)); break;
        case MD_BLOCK_TABLE: b.push(Node(NodeType::Table)); break;
        case MD_BLOCK_THEAD: b.in_thead = true; break;
        case MD_BLOCK_TBODY: b.in_thead = false; break;
        case MD_BLOCK_TR: {
            if (b.in_thead) b.top().head_rows++;
            b.push(Node(NodeType::TableRow));
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            auto align = static_cast<MD_BLOCK_TD_DETAIL*>(detail)->align;
            Align a = align == MD_ALIGN_LEFT ? Align::Left
                    : align == MD_ALIGN_CENTER ? Align::Center
                    : align == MD_ALIGN_RIGHT ? Align::Right : Align::Default;
            // The table is two levels up (Table > TableRow > cell).
            Node& table = *b.stack[b.stack.size() - 2];
            if (table.children.size() == 1) table.aligns.push_back(a);
            auto& cell = b.push(Node(NodeType::TableCell));
            if (type == MD_BLOCK_TH) cell.attrs["header"] = "1";
            break;
        }
    }
    return 0;
}

int leave_block(MD_BLOCKTYPE type, void*, void* ud) {
    auto& b = B(ud);
    switch (type) {
        case MD_BLOCK_DOC:
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            break;
        case MD_BLOCK_HTML:
            b.html_depth--;
            break;
        case MD_BLOCK_P:
            b.finish_paragraph(b.top());
            b.pop();
            break;
        case MD_BLOCK_CODE: {
            auto& n = b.top();
            if (!n.text.empty() && n.text.back() == '\n') n.text.pop_back();
            b.pop();
            break;
        }
        default:
            b.pop();
    }
    return 0;
}

int enter_span(MD_SPANTYPE type, void* detail, void* ud) {
    auto& b = B(ud);
    switch (type) {
        case MD_SPAN_EM: b.push(Node(NodeType::Emph)); break;
        case MD_SPAN_STRONG: b.push(Node(NodeType::Strong)); break;
        case MD_SPAN_DEL: b.push(Node(NodeType::Strike)); break;
        case MD_SPAN_U: b.push(Node(NodeType::Underline)); break;
        case MD_SPAN_CODE: b.push(Node(NodeType::Code)); break;
        case MD_SPAN_A: {
            auto& n = b.push(Node(NodeType::Link));
            n.attrs["href"] = attr_text(static_cast<MD_SPAN_A_DETAIL*>(detail)->href);
            break;
        }
        case MD_SPAN_IMG: {
            auto& n = b.push(Node(NodeType::Image));
            n.attrs["src"] = attr_text(static_cast<MD_SPAN_IMG_DETAIL*>(detail)->src);
            break;
        }
        case MD_SPAN_LATEXMATH: b.push(Node(NodeType::Math)); break;
        case MD_SPAN_LATEXMATH_DISPLAY: {
            auto& n = b.push(Node(NodeType::Math));
            n.attrs["display"] = "1";
            break;
        }
        case MD_SPAN_WIKILINK: b.push(Node(NodeType::Text)); break;
    }
    return 0;
}

int leave_span(MD_SPANTYPE, void*, void* ud) {
    B(ud).pop();
    return 0;
}

int text(MD_TEXTTYPE type, const MD_CHAR* txt, MD_SIZE size, void* ud) {
    auto& b = B(ud);
    if (b.html_depth > 0) return 0;
    std::string_view s(txt, size);
    b.mark_line(txt);
    Node& t = b.top();
    switch (type) {
        case MD_TEXT_NORMAL:
            if (t.type == NodeType::CodeBlock || t.type == NodeType::RawLatex || t.type == NodeType::DisplayMath) t.text += s;
            else b.add_text(s);
            break;
        case MD_TEXT_CODE:
        case MD_TEXT_LATEXMATH:
            t.text += s;
            break;
        case MD_TEXT_NULLCHAR: b.add_text("\xEF\xBF\xBD"); break;
        case MD_TEXT_BR: t.children.emplace_back(NodeType::LineBreak); break;
        case MD_TEXT_SOFTBR: t.children.emplace_back(NodeType::SoftBreak); break;
        case MD_TEXT_ENTITY: b.add_text(decode_entity(s)); break;
        case MD_TEXT_HTML: break;  // inline HTML (comments etc.) is dropped
    }
    return 0;
}

}  // namespace

Document parse_markdown(std::string_view source, const SyntaxStyle& syntax, Diagnostics& diags,
                        const std::string& filename) {
    Preprocessed pre = preprocess(source, syntax, diags, filename);

    Builder b;
    // The stack holds pointers into children vectors. That is safe because only
    // the top node's children ever grow, and the top node itself lives in its
    // parent's vector, which is not touched until the top is popped.
    b.stack.push_back(&b.doc.root);
    b.base = pre.markdown.data();
    b.size = pre.markdown.size();
    b.line_map = &pre.line_map;
    b.line_starts.push_back(0);
    for (size_t i = 0; i < pre.markdown.size(); ++i)
        if (pre.markdown[i] == '\n') b.line_starts.push_back(i + 1);

    MD_PARSER parser{};
    parser.abi_version = 0;
    parser.flags = MD_DIALECT_GITHUB | MD_FLAG_LATEXMATHSPANS | MD_FLAG_NOINDENTEDCODEBLOCKS;
    parser.enter_block = enter_block;
    parser.leave_block = leave_block;
    parser.enter_span = enter_span;
    parser.leave_span = leave_span;
    parser.text = text;

    if (md_parse(pre.markdown.data(), static_cast<MD_SIZE>(pre.markdown.size()), &parser, &b) != 0)
        throw PlaciError("markdown parser failed", filename);

    if (!pre.front_matter.empty()) b.doc.meta = parse_front_matter(pre.front_matter, filename, pre.front_matter_line, diags);
    return std::move(b.doc);
}

}  // namespace placi
