#include "core/style/style_loader.hpp"

#include <ryml_all.hpp>

#include <cctype>
#include <functional>
#include <initializer_list>
#include <set>

namespace placi {

Style::Style() {
    static constexpr const char* kSizes[] = {"16pt", "14pt", "12pt", "", "", ""};
    static constexpr const char* kNumbering[] = {
        "{n}", "{h1}.{n}", "{h1}.{h2}.{n}", "{h1}.{h2}.{h3}.{n}", "{h1}.{h2}.{h3}.{h4}.{n}", "{n}"};
    for (size_t i = 0; i < headings.size(); ++i) {
        auto& h = headings[i];
        h.size = kSizes[i];
        h.numbering = kNumbering[i];
        h.numbered = i < 4;
    }
    headings[0].new_page = true;
    headings[0].space_before = "0pt";
    headings[0].space_after = "24pt";
    headings[0].label_position = "above";
    headings[0].label = "Bab {num}";

    // Built-in lists, tags and sections; a style file may override or extend them.
    lists["gambar"] = {"gambar", {"figures", "figure"}, "Daftar Gambar", "Gambar", "fig", "{h1}.{n}", "figure"};
    lists["tabel"] = {"tabel", {"tables", "table"}, "Daftar Tabel", "Tabel", "tbl", "{h1}.{n}", "table"};
    lists["rumus"] = {"rumus", {"equations", "equation"}, "Daftar Rumus", "Rumus", "eq", "({h1}.{n})", "equation"};

    TagStyle front;
    front.key = "pembuka";
    front.aliases = {"front"};
    front.numbered = false;
    front.page_numbering = PageNumberStyle{"roman", "bottom-center", ""};
    tags["pembuka"] = front;

    TagStyle back;
    back.key = "penutup";
    back.aliases = {"back"};
    back.numbered = false;
    tags["penutup"] = back;

    TagStyle appendix;
    appendix.key = "lampiran";
    appendix.aliases = {"appendix"};
    appendix.label = "Lampiran {num}";
    appendix.numbering = "{Alpha}";
    tags["lampiran"] = appendix;

    SectionStyle landscape;
    landscape.key = "Landscape";
    landscape.aliases = {"landscape", "Lanskap", "JadiLandscape"};
    landscape.orientation = "landscape";
    sections["Landscape"] = landscape;

    SectionStyle two_col;
    two_col.key = "DuaKolom";
    two_col.aliases = {"TwoColumns"};
    two_col.columns = 2;
    sections["DuaKolom"] = two_col;
}

namespace {
template <class T>
const T* find_named(const std::map<std::string, T>& m, const std::string& name) {
    if (auto it = m.find(name); it != m.end()) return &it->second;
    for (auto& [k, v] : m)
        for (auto& a : v.aliases)
            if (a == name) return &v;
    return nullptr;
}
}  // namespace

const ListStyle* Style::find_list(const std::string& name) const { return find_named(lists, name); }
const TagStyle* Style::find_tag(const std::string& name) const { return find_named(tags, name); }
const SectionStyle* Style::find_section(const std::string& name) const { return find_named(sections, name); }

const ListStyle* Style::list_for_label(const std::string& label_prefix) const {
    for (auto& [k, l] : lists)
        if (l.label == label_prefix) return &l;
    return nullptr;
}

const ListStyle* Style::list_of_kind(const std::string& kind) const {
    for (auto& [k, l] : lists)
        if (l.kind == kind) return &l;
    return nullptr;
}

bool is_builtin_tag(const std::string& w) {
    static const std::set<std::string> kBuiltin = {"daftar", "list", "pustaka", "bibliography",
                                                   "sampul", "cover", "halaman-baru", "pagebreak"};
    return kBuiltin.count(w) > 0;
}

bool is_known_rule_property(const std::string& key) {
    static const std::set<std::string> kKnown = {
        "page.orientation",  // portrait | landscape
        "page.break_before", // true: start on a new page
        "table.font_size", "table.longtable", "table.width", "table.borders", "table.placement",
        "table.line_spacing",
        "figure.width", "figure.placement",
        "font_size", "align",
    };
    return kKnown.count(key) > 0;
}

namespace {

std::string S(ryml::csubstr s) { return std::string(s.str ? s.str : "", s.len); }

struct Ctx {
    ryml::Parser* parser;
    ryml::Tree* tree;
    std::string file;
    size_t line_offset;
    Diagnostics& diags;
    const std::vector<fs::path>& search_dirs;
    int depth;

    size_t line(ryml::ConstNodeRef n) const {
        auto loc = tree->location(*parser, n.id());
        return loc.line == ryml::npos ? 0 : loc.line + 1 + line_offset;
    }
    [[noreturn]] void fail(ryml::ConstNodeRef n, const std::string& msg) const { throw PlaciError(msg, file, line(n)); }
    void warn(ryml::ConstNodeRef n, const std::string& msg) const {
        diags.push_back({Diagnostic::Level::Warning, msg, file, line(n)});
    }
    std::string where(ryml::ConstNodeRef n) const { return file + ":" + std::to_string(line(n)); }
};

std::string scalar(const Ctx& c, ryml::ConstNodeRef n, const std::string& key) {
    if (!n.has_val()) c.fail(n, "'" + key + "' must be a single value");
    if (n.val_is_null()) return {};
    return S(n.val());
}

class MapReader {
public:
    MapReader(const Ctx& c, ryml::ConstNodeRef n, std::string path, bool warn_unknown = true)
        : c_(c), n_(n), path_(std::move(path)), warn_unknown_(warn_unknown) {
        if (!n.is_map()) c.fail(n, "'" + path_ + "' must be a mapping (key: value)");
    }
    ~MapReader() {
        if (!warn_unknown_ || std::uncaught_exceptions()) return;
        for (auto ch : n_.children()) {
            auto k = S(ch.key());
            if (!seen_.count(k)) c_.warn(ch, "unknown key '" + full(k) + "' (ignored)");
        }
    }

    std::optional<ryml::ConstNodeRef> get(const std::string& key) {
        seen_.insert(key);
        auto k = ryml::csubstr(key.data(), key.size());
        if (!n_.has_child(k)) return std::nullopt;
        return n_[k];
    }

    void str(const std::string& key, std::string& out) {
        if (auto n = get(key)) out = scalar(c_, *n, full(key));
    }
    void length(const std::string& key, std::string& out, bool allow_empty = false) {
        auto n = get(key);
        if (!n) return;
        auto v = scalar(c_, *n, full(key));
        if (v.empty() && allow_empty) { out.clear(); return; }
        if (!is_tex_length(v)) c_.fail(*n, "'" + full(key) + "': '" + v + "' is not a length (use e.g. 12pt, 1.5cm, 10mm, 1in)");
        out = v;
    }
    void choice(const std::string& key, std::string& out, std::initializer_list<const char*> allowed) {
        auto n = get(key);
        if (!n) return;
        auto v = scalar(c_, *n, full(key));
        std::string list;
        for (auto a : allowed) {
            if (v == a) { out = v; return; }
            list += list.empty() ? "" : ", ";
            list += a;
        }
        c_.fail(*n, "'" + full(key) + "': '" + v + "' is not one of: " + list);
    }
    void boolean(const std::string& key, bool& out) {
        auto n = get(key);
        if (!n) return;
        auto v = to_lower(scalar(c_, *n, full(key)));
        if (v == "true" || v == "yes" || v == "on") out = true;
        else if (v == "false" || v == "no" || v == "off") out = false;
        else c_.fail(*n, "'" + full(key) + "' must be true or false");
    }
    void number(const std::string& key, double& out, double lo, double hi) {
        auto n = get(key);
        if (!n) return;
        auto v = scalar(c_, *n, full(key));
        try {
            size_t used = 0;
            double d = std::stod(v, &used);
            if (used != v.size()) throw 0;
            if (d < lo || d > hi) c_.fail(*n, "'" + full(key) + "' must be between " + format_number(lo) + " and " + format_number(hi));
            out = d;
        } catch (const PlaciError&) {
            throw;
        } catch (...) {
            c_.fail(*n, "'" + full(key) + "' must be a number");
        }
    }
    void integer(const std::string& key, int& out, int lo, int hi) {
        double d = out;
        number(key, d, lo, hi);
        out = static_cast<int>(d);
    }
    void map(const std::string& key, const std::function<void(MapReader&)>& f) {
        if (auto n = get(key)) {
            MapReader sub(c_, *n, full(key));
            f(sub);
        }
    }

    bool has(const std::string& key) const { return n_.has_child(ryml::csubstr(key.data(), key.size())); }

    // A scalar or a list of scalars.
    void strings(const std::string& key, std::vector<std::string>& out) {
        auto n = get(key);
        if (!n) return;
        out.clear();
        if (n->is_seq()) {
            for (auto item : n->children()) out.push_back(scalar(c_, item, full(key)));
        } else {
            out.push_back(scalar(c_, *n, full(key)));
        }
    }

    // A mapping of user-chosen names to mappings: `lists: { gambar: {...}, grafik: {...} }`.
    void named_maps(const std::string& key, const std::function<void(const std::string&, MapReader&)>& f) {
        auto n = get(key);
        if (!n) return;
        if (!n->is_map()) c_.fail(*n, "'" + full(key) + "' must be a mapping");
        for (auto ch : n->children()) {
            std::string name = S(ch.key());
            for (char ch_c : name)
                if (std::isspace(static_cast<unsigned char>(ch_c)))
                    c_.fail(ch, "name '" + name + "' in '" + full(key) + "' must not contain spaces");
            MapReader sub(c_, ch, full(key) + "." + name);
            f(name, sub);
        }
    }

    template <class T, class Read>
    void optional(const std::string& key, std::optional<T>& out, Read read) {
        if (!has(key)) return;
        T v = out.value_or(T{});
        read(v);
        out = v;
    }

    std::string full(const std::string& key) const { return path_.empty() ? key : path_ + "." + key; }
    const Ctx& ctx() const { return c_; }

private:
    const Ctx& c_;
    ryml::ConstNodeRef n_;
    std::string path_;
    std::set<std::string> seen_;
    bool warn_unknown_;
};

void read_caption(MapReader& r, CaptionStyle& c) {
    r.choice("position", c.position, {"above", "below"});
    r.choice("align", c.align, {"center", "left", "justify"});
    r.str("separator", c.separator);
    r.boolean("label_bold", c.label_bold);
    r.length("font_size", c.font_size, true);
}

void read_heading(MapReader& r, HeadingStyle& h) {
    r.boolean("numbered", h.numbered);
    r.str("numbering", h.numbering);
    r.str("label", h.label);
    r.choice("label_position", h.label_position, {"inline", "above"});
    r.length("label_sep", h.label_sep);
    r.choice("align", h.align, {"left", "center", "right"});
    r.length("size", h.size, true);
    r.boolean("bold", h.bold);
    r.boolean("italic", h.italic);
    r.boolean("uppercase", h.uppercase);
    r.boolean("new_page", h.new_page);
    r.length("space_before", h.space_before);
    r.length("space_after", h.space_after);
    r.boolean("in_toc", h.in_toc);
}

void read_page_number(MapReader& r, PageNumberStyle& p) {
    r.choice("style", p.style, {"arabic", "roman", "Roman", "alpha", "Alpha", "none"});
    static constexpr std::initializer_list<const char*> kPos = {
        "top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right", "none"};
    r.choice("position", p.position, kPos);
    r.choice("first_page", p.first_page, kPos);
}

void read_list(MapReader& r, ListStyle& l) {
    r.strings("aliases", l.aliases);
    r.str("title", l.title);
    r.str("prefix", l.prefix);
    r.str("label", l.label);
    r.str("numbering", l.numbering);
    r.choice("kind", l.kind, {"figure", "table", "equation", "float"});
    if (l.label.empty()) l.label = l.key;
    if (l.prefix.empty()) l.prefix = l.key;
    if (l.title.empty()) l.title = l.prefix;
}

void read_tag(MapReader& r, TagStyle& t, const PageNumberStyle& main_numbering) {
    r.strings("aliases", t.aliases);
    r.optional("numbered", t.numbered, [&](bool& v) { r.boolean("numbered", v); });
    if (r.has("page_numbering")) {
        PageNumberStyle p = t.page_numbering.value_or(main_numbering);
        r.map("page_numbering", [&](MapReader& m) {
            // A new position without first_page means "the same on first pages".
            if (m.has("position") && !m.has("first_page")) p.first_page.clear();
            read_page_number(m, p);
        });
        t.page_numbering = p;
    }
    r.optional("label", t.label, [&](std::string& v) { r.str("label", v); });
    r.optional("numbering", t.numbering, [&](std::string& v) { r.str("numbering", v); });
    r.optional("align", t.align, [&](std::string& v) { r.choice("align", v, {"left", "center", "right"}); });
    r.optional("uppercase", t.uppercase, [&](bool& v) { r.boolean("uppercase", v); });
    r.optional("new_page", t.new_page, [&](bool& v) { r.boolean("new_page", v); });
    r.optional("in_toc", t.in_toc, [&](bool& v) { r.boolean("in_toc", v); });
    r.boolean("hide_title", t.hide_title);
    r.str("template", t.template_tex);
    r.map("bibliography", [&](MapReader& b) {
        b.str("style", t.bibliography_style);
        b.choice("citation", t.citation, {"author-year", "numeric"});
    });
}

void read_section(MapReader& r, SectionStyle& s) {
    r.strings("aliases", s.aliases);
    r.choice("orientation", s.orientation, {"portrait", "landscape"});
    r.integer("columns", s.columns, 1, 4);
    r.length("font_size", s.font_size, true);
    r.map("margin", [&](MapReader& m) {
        m.length("top", s.margin.top);
        m.length("bottom", s.margin.bottom);
        m.length("left", s.margin.left);
        m.length("right", s.margin.right);
    });
}

CmpOp parse_op(const Ctx& c, ryml::ConstNodeRef n, const std::string& op) {
    if (op == "eq") return CmpOp::Eq;
    if (op == "ne") return CmpOp::Ne;
    if (op == "gt") return CmpOp::Gt;
    if (op == "gte") return CmpOp::Gte;
    if (op == "lt") return CmpOp::Lt;
    if (op == "lte") return CmpOp::Lte;
    if (op == "contains") return CmpOp::Contains;
    c.fail(n, "unknown operator '" + op + "' (use eq, ne, gt, gte, lt, lte, contains)");
}

void flatten_set(const Ctx& c, ryml::ConstNodeRef n, const std::string& prefix, Rule& rule) {
    for (auto ch : n.children()) {
        std::string key = prefix.empty() ? S(ch.key()) : prefix + "." + S(ch.key());
        if (ch.is_map()) {
            flatten_set(c, ch, key, rule);
            continue;
        }
        if (!is_known_rule_property(key)) c.fail(ch, "rule sets unknown property '" + key + "'");
        rule.set.emplace_back(key, scalar(c, ch, key));
    }
}

void read_rules(const Ctx& c, ryml::ConstNodeRef n, std::vector<Rule>& rules) {
    if (!n.is_seq()) c.fail(n, "'rules' must be a list (each item starts with '- ')");
    for (auto item : n.children()) {
        Rule rule;
        rule.source = c.where(item);
        MapReader r(c, item, "rules[]");
        auto when = r.get("when");
        auto set = r.get("set");
        if (!when) c.fail(item, "rule is missing 'when'");
        if (!set) c.fail(item, "rule is missing 'set'");
        if (!when->is_map()) c.fail(*when, "'when' must be a mapping");
        for (auto cond : when->children()) {
            auto key = S(cond.key());
            if (key == "element") {
                rule.element = scalar(c, cond, key);
                static const std::set<std::string> kElements = {"table", "figure", "heading", "paragraph", "code", "math", "list", "quote", "*"};
                if (!kElements.count(rule.element)) c.fail(cond, "unknown element '" + rule.element + "'");
                continue;
            }
            if (cond.is_map()) {
                for (auto opn : cond.children())
                    rule.when.push_back({key, parse_op(c, opn, S(opn.key())), scalar(c, opn, key)});
            } else {
                rule.when.push_back({key, CmpOp::Eq, scalar(c, cond, key)});
            }
        }
        if (rule.element.empty()) rule.element = "*";
        if (!set->is_map()) c.fail(*set, "'set' must be a mapping");
        flatten_set(c, *set, "", rule);
        rules.push_back(std::move(rule));
    }
}

struct ParsedYaml {
    ryml::EventHandlerTree handler;
    ryml::Parser parser{&handler, ryml::ParserOptions().locations(true)};
    ryml::Tree tree;
};

// ryml's default handlers print to stderr before throwing; ours only throw.
void install_quiet_callbacks() {
    static const bool done = [] {
        ryml::Callbacks cb;
        cb.set_error_basic([](ryml::csubstr msg, ryml::ErrorDataBasic const& e, void*) {
            throw ryml::ExceptionBasic(msg, e);
        });
        cb.set_error_parse([](ryml::csubstr msg, ryml::ErrorDataParse const& e, void*) {
            throw ryml::ExceptionParse(msg, e);
        });
        cb.set_error_visit([](ryml::csubstr msg, ryml::ErrorDataVisit const& e, void*) {
            throw ryml::ExceptionVisit(msg, e);
        });
        ryml::set_callbacks(cb);
        return true;
    }();
    (void)done;
}

std::unique_ptr<ParsedYaml> parse_yaml(std::string_view yaml, const std::string& filename, size_t line_offset) {
    install_quiet_callbacks();
    auto p = std::make_unique<ParsedYaml>();
    try {
        p->tree = ryml::parse_in_arena(&p->parser, ryml::to_csubstr(filename), ryml::csubstr(yaml.data(), yaml.size()));
    } catch (const ryml::ExceptionParse& e) {
        auto loc = e.errdata_parse.ymlloc;
        size_t line = loc.line == ryml::npos ? 0 : loc.line + 1 + line_offset;
        throw PlaciError(std::string("YAML syntax error: ") + e.what(), filename, line);
    } catch (const std::exception& e) {
        throw PlaciError(std::string("YAML error: ") + e.what(), filename);
    }
    return p;
}

void apply_style(Style& st, std::string_view yaml, const std::string& filename, Diagnostics& diags,
                 const std::vector<fs::path>& search_dirs, int depth);

void apply_root(Style& st, MapReader& r) {
    const Ctx& c = r.ctx();
    if (auto ext = r.get("extends")) {
        auto name = scalar(c, *ext, "extends");
        if (c.depth > 8) c.fail(*ext, "'extends' nesting is too deep (cycle?)");
        auto base_dir = u8path(c.file).parent_path();
        auto path = find_style(name, base_dir, c.search_dirs);
        if (!path) c.fail(*ext, "base style '" + name + "' not found");
        apply_style(st, read_file(*path), path_str(*path), c.diags, c.search_dirs, c.depth + 1);
    }
    r.str("name", st.name);
    r.choice("language", st.language, {"indonesian", "english", "malay"});
    r.map("page", [&](MapReader& p) {
        p.choice("size", st.page.size, {"A4", "A5", "B5", "F4", "letter", "legal"});
        p.boolean("twoside", st.page.twoside);
        p.map("margin", [&](MapReader& m) {
            m.length("top", st.page.margin.top);
            m.length("bottom", st.page.margin.bottom);
            m.length("left", st.page.margin.left);
            m.length("right", st.page.margin.right);
        });
    });
    r.map("font", [&](MapReader& f) {
        f.str("family", st.font.family);
        f.str("sans", st.font.sans);
        f.str("mono", st.font.mono);
        f.length("size", st.font.size);
        if (!to_points(st.font.size)) c.fail(*f.get("size"), "'font.size' must be an absolute size such as 12pt");
    });
    r.map("paragraph", [&](MapReader& p) {
        p.number("line_spacing", st.paragraph.line_spacing, 0.5, 4.0);
        p.length("indent", st.paragraph.indent);
        p.boolean("indent_first", st.paragraph.indent_first);
        p.length("space_before", st.paragraph.space_before);
        p.length("space_after", st.paragraph.space_after);
        p.choice("align", st.paragraph.align, {"justify", "left", "right", "center"});
    });
    r.map("headings", [&](MapReader& h) {
        // `all` applies to every level first, then h1..h6 override it.
        if (auto all = h.get("all")) {
            for (size_t i = 0; i < st.headings.size(); ++i) {
                MapReader each(c, *all, "headings.all", /*warn_unknown=*/i == 0);
                read_heading(each, st.headings[i]);
            }
        }
        for (size_t i = 0; i < st.headings.size(); ++i)
            h.map("h" + std::to_string(i + 1), [&](MapReader& l) { read_heading(l, st.headings[i]); });
    });
    r.map("page_numbering", [&](MapReader& p) { read_page_number(p, st.page_numbering); });
    r.map("figure", [&](MapReader& f) {
        f.str("placement", st.figure.placement);
        f.str("width", st.figure.width);
        f.map("caption", [&](MapReader& cap) { read_caption(cap, st.figure.caption); });
    });
    r.map("table", [&](MapReader& t) {
        t.str("placement", st.table.placement);
        t.length("font_size", st.table.font_size, true);
        t.choice("borders", st.table.borders, {"grid", "booktabs", "horizontal", "none"});
        t.boolean("header_bold", st.table.header_bold);
        t.str("header_background", st.table.header_background);
        t.choice("width", st.table.width, {"auto", "full"});
        t.number("line_spacing", st.table.line_spacing, 0.5, 4.0);
        t.map("caption", [&](MapReader& cap) { read_caption(cap, st.table.caption); });
    });
    r.map("bibliography", [&](MapReader& b) {
        b.str("style", st.bibliography.style);
        b.choice("citation", st.bibliography.citation, {"author-year", "numeric"});
        b.str("title", st.bibliography.title);
        b.boolean("in_toc", st.bibliography.in_toc);
    });
    r.map("contents", [&](MapReader& t) { t.integer("depth", st.contents.depth, 0, 5); });
    r.map("cross_reference", [&](MapReader& x) { x.boolean("prefix", st.cross_reference.prefix); });
    r.map("syntax", [&](MapReader& sx) {
        sx.str("tag_open", st.syntax.tag_open);
        sx.str("tag_close", st.syntax.tag_close);
        if (st.syntax.tag_open.empty() || st.syntax.tag_close.empty())
            c.fail(*r.get("syntax"), "'syntax.tag_open' and 'syntax.tag_close' must not be empty");
    });
    r.named_maps("lists", [&](const std::string& key, MapReader& l) {
        auto& ls = st.lists[key];
        ls.key = key;
        read_list(l, ls);
    });
    r.named_maps("tags", [&](const std::string& key, MapReader& t) {
        if (is_builtin_tag(key)) c.fail(*r.get("tags"), "tag name '" + key + "' is reserved");
        auto& ts = st.tags[key];
        ts.key = key;
        read_tag(t, ts, st.page_numbering);
    });
    r.named_maps("sections", [&](const std::string& key, MapReader& s) {
        auto& ss = st.sections[key];
        ss.key = key;
        read_section(s, ss);
    });
    r.str("cover", st.cover);
    r.str("preamble", st.preamble);
    if (auto rules = r.get("rules")) read_rules(c, *rules, st.rules);
}

void apply_style(Style& st, std::string_view yaml, const std::string& filename, Diagnostics& diags,
                 const std::vector<fs::path>& search_dirs, int depth) {
    auto parsed = parse_yaml(yaml, filename, 0);
    Ctx c{&parsed->parser, &parsed->tree, filename, 0, diags, search_dirs, depth};
    auto root = parsed->tree.crootref();
    if (root.is_doc() && !root.is_map() && !root.has_children()) return;  // empty file
    MapReader r(c, root, "");
    apply_root(st, r);
    st.base_dir = path_str(u8path(filename).parent_path());
}

void flatten_meta(const Ctx& c, ryml::ConstNodeRef n, const std::string& prefix, std::map<std::string, std::string>& out) {
    for (auto ch : n.children()) {
        std::string key = prefix.empty() ? S(ch.key()) : prefix + "." + S(ch.key());
        if (ch.is_map()) {
            flatten_meta(c, ch, key, out);
        } else if (ch.is_seq()) {
            std::string joined;
            for (auto item : ch.children()) {
                if (!item.has_val()) continue;
                if (!joined.empty()) joined += ", ";
                joined += S(item.val());
            }
            out[key] = joined;
        } else {
            out[key] = scalar(c, ch, key);
        }
    }
}

}  // namespace

Style load_style_string(std::string_view yaml, const std::string& filename, Diagnostics& diags,
                        const std::vector<fs::path>& search_dirs) {
    Style st;
    apply_style(st, yaml, filename, diags, search_dirs, 0);
    if (st.name.empty()) st.name = path_str(u8path(filename).stem());
    return st;
}

Style load_style(const fs::path& file, Diagnostics& diags, const std::vector<fs::path>& search_dirs) {
    return load_style_string(read_file(file), path_str(file), diags, search_dirs);
}

std::optional<fs::path> find_style(const std::string& name, const fs::path& base_dir,
                                   const std::vector<fs::path>& search_dirs) {
    auto candidates = [&](const fs::path& dir) {
        std::vector<fs::path> c{dir / u8path(name)};
        if (u8path(name).extension().empty()) {
            c.push_back(dir / u8path(name + ".yaml"));
            c.push_back(dir / u8path(name + ".yml"));
        }
        return c;
    };
    // A path that exists as given (absolute, or relative to the working directory) wins.
    if (std::error_code ec; u8path(name).has_extension() && fs::is_regular_file(u8path(name), ec)) return u8path(name);
    std::vector<fs::path> dirs{base_dir};
    dirs.insert(dirs.end(), search_dirs.begin(), search_dirs.end());
    for (auto& d : dirs)
        for (auto& p : candidates(d)) {
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) return p;
        }
    return std::nullopt;
}

std::map<std::string, std::string> parse_front_matter(const std::string& yaml, const std::string& filename,
                                                      size_t first_line, Diagnostics& diags) {
    std::map<std::string, std::string> out;
    size_t offset = first_line ? first_line - 1 : 0;
    auto parsed = parse_yaml(yaml, filename, offset);
    std::vector<fs::path> none;
    Ctx c{&parsed->parser, &parsed->tree, filename, offset, diags, none, 0};
    auto root = parsed->tree.crootref();
    if (!root.is_map()) {
        if (root.has_children() || root.has_val())
            diags.push_back({Diagnostic::Level::Warning, "front matter is not a key: value mapping (ignored)", filename, first_line});
        return out;
    }
    flatten_meta(c, root, "", out);
    return out;
}

void apply_document_lists(Style& style, const std::map<std::string, std::string>& meta, Diagnostics& diags,
                          const std::string& filename) {
    const std::string prefix = "lists.";
    for (auto& [k, v] : meta) {
        if (k.compare(0, prefix.size(), prefix) != 0) continue;
        auto rest = k.substr(prefix.size());
        auto dot = rest.find('.');
        if (dot == std::string::npos) {
            diags.push_back({Diagnostic::Level::Warning, "front matter '" + k + "' must be a mapping of list fields", filename});
            continue;
        }
        std::string name = rest.substr(0, dot), field = rest.substr(dot + 1);
        auto& l = style.lists[name];
        if (l.key.empty()) l.key = name;
        if (field == "title") l.title = v;
        else if (field == "prefix") l.prefix = v;
        else if (field == "label") l.label = v;
        else if (field == "numbering") l.numbering = v;
        else if (field == "kind") {
            if (v != "figure" && v != "table" && v != "equation" && v != "float")
                diags.push_back({Diagnostic::Level::Warning, "list '" + name + "': unknown kind '" + v + "'", filename});
            else
                l.kind = v;
        } else if (field == "aliases") {
            l.aliases.clear();
            size_t start = 0;
            while (start <= v.size()) {
                size_t comma = v.find(',', start);
                auto a = trim(std::string_view(v).substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                if (!a.empty()) l.aliases.emplace_back(a);
                if (comma == std::string::npos) break;
                start = comma + 1;
            }
        } else {
            diags.push_back({Diagnostic::Level::Warning, "list '" + name + "': unknown field '" + field + "'", filename});
        }
    }
    for (auto& [name, l] : style.lists) {
        if (l.label.empty()) l.label = name;
        if (l.prefix.empty()) l.prefix = name;
        if (l.title.empty()) l.title = l.prefix;
    }
}

}  // namespace placi
