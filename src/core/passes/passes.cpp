#include "core/passes/passes.hpp"

#include <cctype>
#include <functional>

#include "core/parser/preprocess.hpp"

namespace placi {

namespace {

void apply_attrs(Node& n, const AttrBlock& ab) {
    if (!ab.id.empty()) n.attrs["id"] = ab.id;
    for (auto& c : ab.classes) n.classes.push_back(c);
    for (auto& [k, v] : ab.kv) n.attrs[k] = v;
    for (auto& w : ab.words) n.tags.push_back(w);
}

bool is_blank_inline(const Node& n) {
    if (n.type == NodeType::SoftBreak) return true;
    return n.type == NodeType::Text && trim(n.text).empty();
}

// Children of `n` that are not whitespace.
std::vector<Node*> significant(Node& n) {
    std::vector<Node*> out;
    for (auto& c : n.children)
        if (!is_blank_inline(c)) out.push_back(&c);
    return out;
}

// Calls f on every vector of block-level children in the tree.
void for_each_block_list(Node& n, const std::function<void(std::vector<Node>&)>& f) {
    if (is_inline(n.type)) return;
    f(n.children);
    for (auto& c : n.children) for_each_block_list(c, f);
}

// Calls f on every inline container (paragraph, heading, cell, emphasis, captions...).
void for_each_inline_list(Node& n, const std::function<void(std::vector<Node>&)>& f) {
    if (n.type == NodeType::Code || n.type == NodeType::Math || n.type == NodeType::Link) return;
    if (!n.caption.empty()) {
        f(n.caption);
        for (auto& c : n.caption) for_each_inline_list(c, f);
    }
    bool has_inline = false;
    for (auto& c : n.children) has_inline |= is_inline(c.type);
    if (has_inline) f(n.children);
    for (auto& c : n.children) for_each_inline_list(c, f);
}

void replace_all(std::string& s, std::string_view from, std::string_view to) {
    for (size_t pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size()))
        s.replace(pos, from.size(), to);
}

// ---------------------------------------------------------------- sections

void fold_list(std::vector<Node>& kids, Diagnostics& diags) {
    for (auto& k : kids)
        if (!is_inline(k.type)) fold_list(k.children, diags);

    bool any = false;
    for (auto& k : kids) any |= k.type == NodeType::DivMarker;
    if (!any) return;

    std::vector<Node> out;
    std::vector<Node> open;
    auto target = [&]() -> std::vector<Node>& { return open.empty() ? out : open.back().children; };

    for (auto& k : kids) {
        if (k.type != NodeType::DivMarker) {
            target().push_back(std::move(k));
            continue;
        }
        const std::string kind = k.attr("kind");
        if (kind == "pagebreak") {
            Node pb(NodeType::PageBreak);
            pb.line = k.line;
            target().push_back(std::move(pb));
        } else if (kind == "open") {
            Node div(NodeType::Div);
            div.attrs["class"] = k.attr("class");
            div.line = k.line;
            open.push_back(std::move(div));
        } else if (kind == "close" && !open.empty()) {
            // The preprocessor guarantees open/close pairs are balanced.
            Node div = std::move(open.back());
            open.pop_back();
            target().push_back(std::move(div));
        }
    }
    while (!open.empty()) {  // only reachable if a section spans list items etc.
        diags.push_back({Diagnostic::Level::Warning, "'::: " + open.back().attr("class") + "' ends inside another block",
                         {}, open.back().line});
        Node div = std::move(open.back());
        open.pop_back();
        target().push_back(std::move(div));
    }
    kids = std::move(out);
}

// ---------------------------------------------------------------- citations

bool key_start(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
bool key_char(char c) {
    return key_start(c) || c == ':' || c == '.' || c == '-' || c == '/' || c == '+' || c == '#' || c == '&';
}

// Reads a citation key at s[i] (after '@'); strips trailing punctuation.
std::string read_key(std::string_view s, size_t& i) {
    size_t b = i;
    if (i >= s.size() || !key_start(s[i])) return {};
    while (i < s.size() && key_char(s[i])) ++i;
    while (i > b && (s[i - 1] == '.' || s[i - 1] == ':' || s[i - 1] == '-' || s[i - 1] == '/')) --i;
    return std::string(s.substr(b, i - b));
}

using Prefixes = std::vector<std::string>;

bool is_xref_key(std::string_view k, const Prefixes& prefixes) {
    size_t colon = k.find(':');
    if (colon == std::string_view::npos) return false;
    for (auto& p : prefixes)
        if (k.substr(0, colon) == p) return true;
    return false;
}

Node make_ref(const std::string& key, const std::string& mode, const Prefixes& prefixes) {
    if (is_xref_key(key, prefixes)) {
        Node x(NodeType::CrossRef);
        x.attrs["id"] = key;
        return x;
    }
    Node c(NodeType::Citation);
    c.attrs["keys"] = key;
    c.attrs["mode"] = mode;
    return c;
}

// Parses the inside of "[...]" as a citation group: "@a, p. 3; -@b".
std::optional<Node> parse_bracket(std::string_view body, const Prefixes& prefixes) {
    std::vector<std::string> keys;
    std::string locator;
    bool all_suppressed = true;
    size_t start = 0;
    while (start <= body.size()) {
        size_t semi = body.find(';', start);
        auto item = trim(body.substr(start, semi == std::string_view::npos ? std::string_view::npos : semi - start));
        bool suppress = false;
        if (!item.empty() && item[0] == '-') {
            suppress = true;
            item.remove_prefix(1);
        }
        if (item.empty() || item[0] != '@') return std::nullopt;
        size_t i = 1;
        auto key = read_key(item, i);
        if (key.empty()) return std::nullopt;
        auto rest = trim(item.substr(i));
        if (!rest.empty()) {
            if (rest[0] != ',') return std::nullopt;
            locator = std::string(trim(rest.substr(1)));
        }
        all_suppressed &= suppress;
        keys.push_back(key);
        if (semi == std::string_view::npos) break;
        start = semi + 1;
    }
    if (keys.empty()) return std::nullopt;
    if (keys.size() == 1 && is_xref_key(keys[0], prefixes)) return make_ref(keys[0], "paren", prefixes);
    for (auto& k : keys)
        if (is_xref_key(k, prefixes)) return std::nullopt;  // mixing refs and citations: leave as text
    std::string joined;
    for (auto& k : keys) joined += (joined.empty() ? "" : ",") + k;
    Node c(NodeType::Citation);
    c.attrs["keys"] = joined;
    c.attrs["mode"] = all_suppressed ? "year" : "paren";
    if (!locator.empty()) c.attrs["locator"] = locator;
    return c;
}

void split_citations(std::vector<Node>& kids, const Prefixes& prefixes) {
    bool any = false;
    for (auto& k : kids) any |= k.type == NodeType::Text && k.text.find('@') != std::string::npos;
    if (!any) return;
    std::vector<Node> out;
    for (auto& k : kids) {
        if (k.type != NodeType::Text || k.text.find('@') == std::string::npos) {
            out.push_back(std::move(k));
            continue;
        }
        std::string_view s = k.text;
        std::string buf;
        auto flush = [&] {
            if (!buf.empty()) out.emplace_back(NodeType::Text, std::move(buf));
            buf.clear();
        };
        size_t i = 0;
        while (i < s.size()) {
            char ch = s[i];
            if (ch == '[') {
                size_t close = s.find(']', i + 1);
                if (close != std::string_view::npos) {
                    if (auto cit = parse_bracket(s.substr(i + 1, close - i - 1), prefixes)) {
                        flush();
                        out.push_back(std::move(*cit));
                        i = close + 1;
                        continue;
                    }
                }
            } else if (ch == '@' && (i == 0 || !std::isalnum(static_cast<unsigned char>(s[i - 1])))) {
                size_t j = i + 1;
                auto key = read_key(s, j);
                if (!key.empty()) {
                    flush();
                    out.push_back(make_ref(key, "text", prefixes));
                    i = j;
                    continue;
                }
            }
            buf += ch;
            ++i;
        }
        flush();
    }
    kids = std::move(out);
}


}  // namespace

void fold_divs(Node& root, Diagnostics& diags) { fold_list(root.children, diags); }

void apply_heading_tags(Node& root, const SyntaxStyle& sx) {
    for_each_block_list(root, [&](std::vector<Node>& kids) {
        for (auto& h : kids) {
            if (h.type != NodeType::Heading || h.children.empty() || h.children.back().type != NodeType::Text) continue;
            auto body = take_trailing_attrs(h.children.back().text, sx.tag_open, sx.tag_close);
            if (!body) continue;
            auto ab = parse_attr_block(*body);
            if (!ab) continue;
            // Built-in words: "daftar X" / "list X", "pustaka" / "bibliography", "sampul" / "cover".
            AttrBlock rest = *ab;
            rest.words.clear();
            for (size_t i = 0; i < ab->words.size(); ++i) {
                const auto& w = ab->words[i];
                if ((w == "daftar" || w == "list") && i + 1 < ab->words.size()) {
                    h.attrs["list"] = ab->words[++i];
                } else if (w == "pustaka" || w == "bibliography") {
                    h.attrs["role"] = "bibliography";
                } else if (w == "sampul" || w == "cover") {
                    h.attrs["role"] = "cover";
                } else {
                    rest.words.push_back(w);
                }
            }
            apply_attrs(h, rest);
            if (h.children.back().text.empty()) h.children.pop_back();
        }
    });
}

void build_figures(Node& root, const SyntaxStyle& sx) {
    // 1) "{...}" right after an inline image belongs to the image.
    for_each_inline_list(root, [&](std::vector<Node>& kids) {
        for (size_t i = 0; i + 1 < kids.size(); ++i) {
            if (kids[i].type != NodeType::Image || kids[i + 1].type != NodeType::Text) continue;
            if (auto body = take_leading_attrs(kids[i + 1].text, sx.tag_open, sx.tag_close))
                if (auto ab = parse_attr_block(*body)) apply_attrs(kids[i], *ab);
        }
    });
    // 2) A paragraph that holds nothing but one image is a figure.
    for_each_block_list(root, [](std::vector<Node>& kids) {
        for (auto& p : kids) {
            if (p.type != NodeType::Paragraph) continue;
            auto sig = significant(p);
            if (sig.size() != 1 || sig[0]->type != NodeType::Image) continue;
            Node img = std::move(*sig[0]);
            Node fig(NodeType::Figure);
            fig.line = p.line;
            fig.attrs = std::move(img.attrs);
            fig.classes = std::move(img.classes);
            fig.caption = std::move(img.children);
            p = std::move(fig);
        }
    });
}

void attach_table_captions(Node& root, const SyntaxStyle& sx) {
    auto caption_of = [](Node& p) -> bool {
        if (p.type != NodeType::Paragraph || p.children.empty() || p.children[0].type != NodeType::Text) return false;
        std::string& t = p.children[0].text;
        for (std::string_view prefix : {"Table:", "Tabel:", ":"}) {
            if (t.compare(0, prefix.size(), prefix) == 0) {
                t.erase(0, prefix.size());
                while (!t.empty() && std::isspace(static_cast<unsigned char>(t[0]))) t.erase(0, 1);
                return true;
            }
        }
        return false;
    };
    for_each_block_list(root, [&](std::vector<Node>& kids) {
        for (size_t i = 0; i < kids.size(); ++i) {
            if (kids[i].type != NodeType::Table || !kids[i].caption.empty()) continue;
            size_t cap = kids.size();
            if (i > 0 && caption_of(kids[i - 1])) cap = i - 1;
            else if (i + 1 < kids.size() && caption_of(kids[i + 1])) cap = i + 1;
            if (cap == kids.size()) continue;
            Node& table = kids[i];
            Node& para = kids[cap];
            if (!para.children.empty() && para.children.back().type == NodeType::Text)
                if (auto body = take_trailing_attrs(para.children.back().text, sx.tag_open, sx.tag_close))
                    if (auto ab = parse_attr_block(*body)) apply_attrs(table, *ab);
            table.caption = std::move(para.children);
            if (cap < i) table.line = para.line;
            kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(cap));
            if (cap < i) --i;
        }
    });
}

void build_display_math(Node& root, const SyntaxStyle& sx) {
    for_each_block_list(root, [&](std::vector<Node>& kids) {
        for (auto& p : kids) {
            // ```math {#eq:x caption="..."}
            if (p.type == NodeType::DisplayMath) {
                auto info = p.attr("info");
                p.attrs.erase("info");
                p.attrs.erase("lang");
                if (!p.text.empty() && p.text.back() == '\n') p.text.pop_back();
                if (!info.empty())
                    if (auto body = take_trailing_attrs(info, sx.tag_open, sx.tag_close))
                        if (auto ab = parse_attr_block(*body)) apply_attrs(p, *ab);
                continue;
            }
            // $$ ... $$ {#eq:x}
            if (p.type != NodeType::Paragraph) continue;
            auto sig = significant(p);
            if (sig.empty() || sig[0]->type != NodeType::Math || sig[0]->attr("display").empty()) continue;
            std::optional<AttrBlock> ab;
            if (sig.size() == 2) {
                if (sig[1]->type != NodeType::Text) continue;
                std::string t(trim(sig[1]->text));
                auto body = take_trailing_attrs(t, sx.tag_open, sx.tag_close);
                if (!body || !trim(t).empty()) continue;
                ab = parse_attr_block(*body);
                if (!ab) continue;
            } else if (sig.size() != 1) {
                continue;
            }
            Node m(NodeType::DisplayMath, std::move(sig[0]->text));
            m.line = p.line;
            if (ab) apply_attrs(m, *ab);
            p = std::move(m);
        }
    });
}

void resolve_citations(Node& root, const std::vector<std::string>& ref_prefixes) {
    for_each_inline_list(root, [&](std::vector<Node>& kids) { split_citations(kids, ref_prefixes); });
}

void restore_escapes(Node& n, const SyntaxStyle& sx) {
    if (n.type == NodeType::Text) {
        replace_all(n.text, kEscapedOpen, sx.tag_open);
        replace_all(n.text, kEscapedClose, sx.tag_close);
    } else if (n.type == NodeType::Code || n.type == NodeType::Math || n.type == NodeType::DisplayMath) {
        replace_all(n.text, kEscapedOpen, "\\" + sx.tag_open);
        replace_all(n.text, kEscapedClose, "\\" + sx.tag_close);
    }
    for (auto& c : n.caption) restore_escapes(c, sx);
    for (auto& c : n.children) restore_escapes(c, sx);
}

DocumentFacts collect_facts(const Node& root) {
    DocumentFacts f;
    std::function<void(const Node&)> walk = [&](const Node& n) {
        if (n.type == NodeType::Citation) f.has_citations = true;
        if (n.type == NodeType::Heading) {
            auto role = n.attr("role");
            if (role == "bibliography" && !f.bibliography_heading) f.bibliography_heading = &n;
            if (role == "cover") f.has_cover_heading = true;
        }
        for (auto& c : n.caption) walk(c);
        for (auto& c : n.children) walk(c);
    };
    walk(root);
    return f;
}

ParseOptions ParseOptions::from_style(const Style& st) {
    ParseOptions o;
    o.syntax = st.syntax;
    o.ref_prefixes = {"sec", "lst"};
    for (auto& [k, l] : st.lists) o.ref_prefixes.push_back(l.label);
    return o;
}

void run_passes(Node& root, Diagnostics& diags, const ParseOptions& opts) {
    fold_divs(root, diags);
    apply_heading_tags(root, opts.syntax);
    build_figures(root, opts.syntax);
    attach_table_captions(root, opts.syntax);
    build_display_math(root, opts.syntax);
    resolve_citations(root, opts.ref_prefixes);
    restore_escapes(root, opts.syntax);
}

}  // namespace placi
