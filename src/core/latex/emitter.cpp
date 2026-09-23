#include "core/latex/emitter.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <functional>
#include <set>

#include "core/latex/escape.hpp"
#include "core/latex/preamble.hpp"
#include "core/passes/passes.hpp"

namespace placi::latex {

namespace {

struct PaperMM { const char* name; double w, h; };
constexpr PaperMM kPapers[] = {
    {"A4", 210, 297}, {"A5", 148, 210}, {"B5", 176, 250}, {"F4", 215, 330},
    {"letter", 215.9, 279.4}, {"legal", 215.9, 355.6},
};

// Width of the text block in points (height of it for landscape pages).
double text_width_pt(const Style& st, bool landscape) {
    double w = 210, h = 297;
    for (auto& p : kPapers)
        if (st.page.size == p.name) { w = p.w; h = p.h; }
    const double mm = 72.27 / 25.4;
    auto len = [&](const std::string& s) { return to_points(s).value_or(3 * 10 * mm); };
    const auto& m = st.page.margin;
    return landscape ? h * mm - len(m.top) - len(m.bottom) : w * mm - len(m.left) - len(m.right);
}

std::string width_expr(const std::string& w) {
    auto t = std::string(trim(w));
    if (!t.empty() && t.back() == '%') {
        try {
            double pct = std::stod(t.substr(0, t.size() - 1));
            return format_number(pct / 100.0) + "\\linewidth";
        } catch (...) {
            return {};
        }
    }
    if (is_tex_length(t)) return t;
    return {};
}

bool is_landscape(const Node& n) { return n.prop("page.orientation") == "landscape"; }

std::string label_prefix(const std::string& id) {
    size_t colon = id.find(':');
    return colon == std::string::npos ? std::string() : id.substr(0, colon);
}

class Emitter {
public:
    Emitter(const Style& st, Diagnostics& d) : plan(make_plan(st)), st_(st), diags_(d) {
        for (int lvl = 1; lvl <= 6; ++lvl) active_setup_[lvl - 1] = heading_setup(lvl, st_.headings[lvl - 1], false);
    }

    std::string out;
    const Document* doc = nullptr;
    const EmitOptions* opts = nullptr;
    DocPlan plan;
    DocumentFacts facts;
    std::set<std::string> ids;       // every {#id} in the document
    bool bib_emitted = false;
    LineMap line_map;                // .tex line -> Markdown line

    // Records that whatever is emitted next comes from `md_line`.
    void note_source_line(size_t md_line) {
        if (!md_line) return;
        for (size_t i = counted_; i < out.size(); ++i)
            if (out[i] == '\n') ++tex_line_;
        counted_ = out.size();
        if (line_map.empty() || line_map.back().second != md_line) line_map.emplace_back(tex_line_, md_line);
    }

    void collect_ids(const Node& n) {
        // A CrossRef's "id" is its target, not a label it defines.
        if (auto id = n.attr("id"); !id.empty() && n.type != NodeType::CrossRef) ids.insert(id);
        for (auto& c : n.caption) collect_ids(c);
        for (auto& c : n.children) collect_ids(c);
    }

    // The bibliography style can be set by a tag on the `pustaka` heading.
    void resolve_bibliography() {
        if (!facts.bibliography_heading) return;
        for (auto* t : tags_of(*facts.bibliography_heading)) {
            if (!t->bibliography_style.empty()) plan.bib_style = t->bibliography_style;
            if (!t->citation.empty()) plan.citation = t->citation;
        }
    }

    void start_numbering() {
        current_page_style_ = 0;
        out += page_numbering_switch(0, plan.page_styles[0], nullptr);
    }

    // ------------------------------------------------------------ blocks

    void blocks(const std::vector<Node>& kids) {
        for (size_t i = 0; i < kids.size();) {
            if (is_landscape(kids[i])) {
                out += "\\begin{landscape}\n";
                while (i < kids.size() && is_landscape(kids[i])) block(kids[i++]);
                out += "\\end{landscape}\n\n";
            } else {
                block(kids[i++]);
            }
        }
    }

    // Children that may mix inline runs and blocks (list items).
    void mixed(const std::vector<Node>& kids) {
        std::vector<Node> run;
        auto flush = [&] {
            if (run.empty()) return;
            out += inlines(run);
            out += "\n";
            run.clear();
        };
        for (auto& k : kids) {
            if (is_inline(k.type)) {
                run.push_back(k);
            } else {
                flush();
                block(k);
            }
        }
        flush();
    }

    void block(const Node& n) {
        note_source_line(n.line);
        if (n.prop("page.break_before") == "true") out += "\\clearpage\n";
        switch (n.type) {
            case NodeType::Heading: heading(n); break;
            case NodeType::Paragraph: paragraph(n); break;
            case NodeType::BlockQuote:
                out += "\\begin{quote}\n";
                blocks(n.children);
                out += "\\end{quote}\n\n";
                break;
            case NodeType::BulletList:
            case NodeType::OrderedList: list(n); break;
            case NodeType::ListItem:
                out += "\\item ";
                mixed(n.children);
                break;
            case NodeType::CodeBlock:
                out += "\\begin{Verbatim}[fontsize=\\small,frame=single,baselinestretch=1]\n" + n.text + "\n\\end{Verbatim}\n\n";
                break;
            case NodeType::RawLatex: out += n.text + "\n\n"; break;
            case NodeType::HorizontalRule: out += "\\noindent\\rule{\\linewidth}{0.4pt}\\par\n\n"; break;
            case NodeType::Table: table(n); break;
            case NodeType::Figure: figure(n); break;
            case NodeType::Div: section(n); break;
            case NodeType::PageBreak: out += "\\clearpage\n\n"; break;
            case NodeType::DisplayMath: display_math(n); break;
            case NodeType::DivMarker: break;
            default:
                if (is_inline(n.type)) {
                    out += inlines({n});
                    out += "\n\n";
                } else {
                    blocks(n.children);
                }
        }
    }

    // ------------------------------------------------------------ headings & tags

    std::vector<const TagStyle*> tags_of(const Node& h) {
        std::vector<const TagStyle*> out_tags;
        for (auto& name : h.tags) {
            if (auto* t = st_.find_tag(name)) {
                out_tags.push_back(t);
            } else if (warned_tags_.insert(name).second) {
                diags_.push_back({Diagnostic::Level::Warning, "unknown tag '" + name + "' (define it under 'tags:' in the style)",
                                  {}, h.line});
            }
        }
        return out_tags;
    }

    void heading(const Node& n) {
        const int level = std::clamp(n.level, 1, 6);
        const std::string role = n.attr("role");
        const std::string list_name = n.attr("list");
        auto own = tags_of(n);

        // Scopes: a heading closes every open scope of the same or a deeper level.
        while (!scopes_.empty() && scopes_.back().level >= level) scopes_.pop_back();
        scopes_.push_back({level, own});

        if (role == "cover") {
            cover();
            return;
        }

        // Page numbering in effect for this heading (innermost tag that sets it).
        std::optional<PageNumberStyle> scoped_page_numbering;
        std::optional<bool> scoped_numbered;
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
            for (auto* t : it->tags) {
                if (t->page_numbering && !scoped_page_numbering) scoped_page_numbering = t->page_numbering;
                if (t->numbered && !scoped_numbered) scoped_numbered = t->numbered;
            }
        PageNumberStyle numbering = scoped_page_numbering.value_or(st_.page_numbering);
        size_t idx = plan.page_style_index(numbering);
        if (idx != current_page_style_) {
            const PageNumberStyle* prev = current_page_style_ < plan.page_styles.size() ? &plan.page_styles[current_page_style_] : nullptr;
            PageNumberStyle prev_copy = prev ? *prev : PageNumberStyle{};  // page_styles may grow below
            out += page_numbering_switch(idx, numbering, prev ? &prev_copy : nullptr);
            current_page_style_ = idx;
        }

        // Format of this level, as changed by the heading's own tags.
        HeadingStyle hs = st_.headings[level - 1];
        bool numbering_overridden = false;
        bool hide_title = false;
        std::string templ;
        for (auto* t : own) {
            if (t->label) hs.label = *t->label;
            if (t->numbering) hs.numbering = *t->numbering, numbering_overridden = true;
            if (t->align) hs.align = *t->align;
            if (t->uppercase) hs.uppercase = *t->uppercase;
            if (t->new_page) hs.new_page = *t->new_page;
            if (t->in_toc) hs.in_toc = *t->in_toc;
            hide_title |= t->hide_title;
            if (!t->template_tex.empty()) templ = t->template_tex;
        }
        std::string setup = heading_setup(level, hs, numbering_overridden);
        if (setup != active_setup_[level - 1]) {
            // A different number format starts its own count (Lampiran A, B, ...).
            bool renumber = setup.substr(0, setup.find('\n')) != active_setup_[level - 1].substr(0, active_setup_[level - 1].find('\n'));
            out += setup;
            if (renumber) {
                const std::string cmd = section_command(level);
                // hyperref names anchors after \theH<counter>; restarting the count would
                // reuse chapter.1 etc., so links and bookmarks would jump to the wrong page.
                out += "\\setcounter{" + cmd + "}{0}\\renewcommand{\\theH" + cmd + "}{placi" + std::to_string(++renumbered_) +
                       ".\\arabic{" + cmd + "}}\n";
            }
            active_setup_[level - 1] = setup;
        }

        bool generated = role == "bibliography" || !list_name.empty();
        bool numbered = scoped_numbered.value_or(hs.numbered) && !n.has_class("unnumbered") && !generated;
        bool contents = list_name == "isi" || list_name == "contents";
        bool in_toc = hs.in_toc && !n.has_class("notoc") && !contents && level <= st_.contents.depth;

        if (!hide_title) {
            if (hs.new_page && level > 1) out += "\\clearpage\n";
            std::string cmd = section_command(level);
            std::string title = inlines(n.children);
            if (numbered) {
                out += "\\" + cmd + "{" + title + "}";
            } else {
                out += "\\" + cmd + "*{" + title + "}";
                if (in_toc) out += "\\phantomsection\\addcontentsline{toc}{" + cmd + "}{" + title + "}";
            }
            if (auto id = n.attr("id"); !id.empty()) out += "\\label{" + id + "}";
            out += "\n\n";
        }
        if (!templ.empty()) out += fill(templ, "'" + plain_text(n) + "'", n.line) + "\n\n";
        if (!list_name.empty()) list_of(list_name, n);
        if (role == "bibliography") bibliography(true);
    }

    void list_of(const std::string& name, const Node& h) {
        if (name == "isi" || name == "contents") {
            out += "\\placilistof{toc}\n\n";
            return;
        }
        const ListStyle* l = st_.find_list(name);
        const ListEnv* e = l ? plan.env_for_list(l) : nullptr;
        if (!e) {
            diags_.push_back({Diagnostic::Level::Warning, "unknown list '" + name + "' (define it under 'lists:')", {}, h.line});
            return;
        }
        out += "\\placilistof{" + e->ext + "}\n\n";
    }

    // ------------------------------------------------------------ other blocks

    void paragraph(const Node& n) {
        std::string body = inlines(n.children);
        if (trim(body).empty()) return;
        std::string pre;
        if (auto fs = n.prop("font_size"); !fs.empty()) pre += fontsize_cmd(fs);
        if (auto a = n.prop("align"); !a.empty())
            pre += a == "center" ? "\\Centering" : a == "right" ? "\\RaggedLeft" : a == "left" ? "\\RaggedRight" : "\\justifying";
        if (!pre.empty()) {
            out += "{" + pre + " " + body + "\\par}\n\n";
        } else {
            out += body + "\n\n";
        }
    }

    void list(const Node& n) {
        bool ordered = n.type == NodeType::OrderedList;
        out += ordered ? "\\begin{enumerate}" : "\\begin{itemize}";
        if (ordered && n.attr("start", "1") != "1") out += "[start=" + n.attr("start") + "]";
        out += "\n";
        for (auto& item : n.children) block(item);
        out += ordered ? "\\end{enumerate}\n\n" : "\\end{itemize}\n\n";
    }

    void display_math(const Node& n) {
        auto id = n.attr("id");
        auto caption = n.attr("caption");
        std::string body = n.text;
        bool multiline = body.find("\\\\") != std::string::npos && body.find("\\begin") == std::string::npos;
        if (multiline) body = "\\begin{aligned}\n" + body + "\n\\end{aligned}";
        bool numbered = (!id.empty() || !caption.empty()) && !n.has_class("unnumbered");
        if (!numbered) {
            out += "\\[\n" + body + "\n\\]\n\n";
            return;
        }
        out += "\\begin{equation}" + (id.empty() ? std::string() : "\\label{" + id + "}") + "\n" + body + "\n\\end{equation}\n";
        if (!caption.empty()) {
            const ListEnv* e = env_for(n, "equation");
            out += "\\addcontentsline{" + (e ? e->ext : std::string("loe")) + "}{equation}{\\protect\\numberline{\\theequation}" +
                   escape(caption) + "}\n";
        }
        out += "\n";
    }

    // The list an item belongs to: by its label prefix, else the default list of its kind.
    const ListEnv* env_for(const Node& n, const std::string& kind) {
        if (auto p = label_prefix(n.attr("id")); !p.empty())
            if (auto* l = st_.list_for_label(p))
                if (auto* e = plan.env_for_list(l); e && (e->env != "equation" || kind == "equation")) return e;
        if (auto* l = st_.list_of_kind(kind)) return plan.env_for_list(l);
        return nullptr;
    }

    void open_float(const std::string& env, const std::string& placement) {
        if (multicol_ > 0) {
            // Floats are not allowed inside multicols: typeset in place with \captionof.
            out += "\\begin{center}\\begin{minipage}{\\linewidth}\\centering\\captionsetup{type=" + env + "}\n";
        } else {
            out += "\\begin{" + env + "}[" + placement + "]\n\\centering\n";
        }
    }
    void close_float(const std::string& env) {
        out += multicol_ > 0 ? "\\end{minipage}\\end{center}\n\n" : "\\end{" + env + "}\n\n";
    }

    void figure(const Node& n) {
        std::string src = n.attr("src");
        std::string width = width_expr(n.prop("figure.width", n.attr("width", st_.figure.width)));
        if (width.empty()) {
            diags_.push_back({Diagnostic::Level::Warning, "figure '" + src + "': invalid width, using the style default", {}, n.line});
            width = width_expr(st_.figure.width);
        }
        const ListEnv* e = env_for(n, "figure");
        std::string env = e ? e->env : "figure";
        std::string caption = caption_cmd(n);
        bool above = st_.figure.caption.position == "above";

        open_float(env, n.prop("figure.placement", st_.figure.placement));
        if (above) out += caption;
        if (!n.attr("missing").empty()) {
            out += "\\fbox{\\parbox[c][4cm][c]{" + width + "}{\\centering\\ttfamily " + escape(src) + "}}\n";
        } else {
            out += "\\includegraphics[width=" + width + ",height=0.75\\textheight,keepaspectratio]{" + src + "}\n";
        }
        if (!above) out += caption;
        close_float(env);
    }

    std::string caption_cmd(const Node& n) {
        std::string s;
        if (!n.caption.empty()) s += "\\caption{" + inlines(n.caption) + "}";
        if (auto id = n.attr("id"); !id.empty()) s += "\\label{" + id + "}";
        return s.empty() ? s : s + "\n";
    }

    void table(const Node& t) {
        const int cols = t.column_count();
        if (cols == 0) return;
        const ListEnv* e = env_for(t, "table");
        const std::string env = e ? e->env : "table";
        const bool landscape = is_landscape(t);
        // longtable numbers with the table counter and cannot live in multicols.
        const bool longtable = t.prop("table.longtable") == "true" && env == "table" && multicol_ == 0;
        const std::string borders = t.prop("table.borders", st_.table.borders);
        const std::string width_mode = t.prop("table.width", st_.table.width);
        const std::string font_size = t.prop("table.font_size", st_.table.font_size);
        const std::string placement = t.prop("table.placement", st_.table.placement);
        double spacing = st_.table.line_spacing;
        if (auto ls = t.prop("table.line_spacing"); !ls.empty()) try { spacing = std::stod(ls); } catch (...) {}

        // Column widths: natural (l/c/r) when the table fits, proportional
        // paragraph columns when it would overflow or the style asks for full width.
        std::vector<size_t> longest(static_cast<size_t>(cols), 0);
        bool has_break = false;
        for (auto& row : t.children)
            for (size_t c = 0; c < row.children.size() && c < longest.size(); ++c) {
                longest[c] = std::max(longest[c], plain_text(row.children[c]).size());
                for (auto& in : row.children[c].children) has_break |= in.type == NodeType::LineBreak;
            }
        double font_pt = to_points(font_size.empty() ? st_.font.size : font_size).value_or(12);
        double chars_per_line = text_width_pt(st_, landscape) / (0.5 * font_pt) / std::max(1, multicol_cols_) - 2.0 * cols;
        size_t total = 0;
        for (auto l : longest) total += l;
        bool wrap = width_mode == "full" || has_break || static_cast<double>(total) > chars_per_line;

        const bool grid = borders == "grid";
        std::string spec = grid ? "|" : "";
        double weight_sum = 0;
        for (auto l : longest) weight_sum += static_cast<double>(std::max<size_t>(l, 6));
        for (int c = 0; c < cols; ++c) {
            Align a = c < static_cast<int>(t.aligns.size()) ? t.aligns[static_cast<size_t>(c)] : Align::Default;
            if (wrap) {
                double frac = static_cast<double>(std::max<size_t>(longest[static_cast<size_t>(c)], 6)) / weight_sum;
                std::string just = a == Align::Center ? "\\centering" : a == Align::Right ? "\\raggedleft" : "\\raggedright";
                spec += ">{" + just + "\\arraybackslash}p{\\dimexpr " + format_number(frac) + "\\linewidth-2\\tabcolsep" +
                        (grid ? "-" + format_number(static_cast<double>(cols + 1) / cols) + "\\arrayrulewidth" : "") + "\\relax}";
            } else {
                spec += a == Align::Center ? "c" : a == Align::Right ? "r" : "l";
            }
            if (grid) spec += "|";
        }

        auto top = [&] { return borders == "booktabs" ? "\\toprule\n" : borders == "none" ? "" : "\\hline\n"; };
        auto mid = [&] { return borders == "booktabs" ? "\\midrule\n" : borders == "none" ? "" : "\\hline\n"; };
        auto bottom = [&] { return borders == "booktabs" ? "\\bottomrule\n" : borders == "none" ? "" : "\\hline\n"; };

        auto row_tex = [&](const Node& row, bool header) {
            std::string r;
            if (header && !st_.table.header_background.empty()) r += "\\rowcolor{" + st_.table.header_background + "}";
            for (int c = 0; c < cols; ++c) {
                if (c) r += " & ";
                if (c < static_cast<int>(row.children.size())) {
                    in_cell_ = true;
                    std::string cell = inlines(row.children[static_cast<size_t>(c)].children);
                    in_cell_ = false;
                    r += header && st_.table.header_bold ? "\\textbf{" + cell + "}" : cell;
                }
            }
            return r + " \\\\\n";
        };

        std::string head, body;
        for (size_t i = 0; i < t.children.size(); ++i) {
            bool header = static_cast<int>(i) < t.head_rows;
            (header ? head : body) += row_tex(t.children[i], header);
            if (grid && !(i + 1 == t.children.size())) (header ? head : body) += "\\hline\n";
        }
        if (!grid && t.head_rows > 0) head += mid();

        std::string group_open = "\\begingroup" + fontsize_cmd(font_size) + "\\setstretch{" + format_number(spacing) + "}\n";
        std::string caption = caption_cmd(t);
        bool above = st_.table.caption.position != "below";

        if (longtable) {
            out += group_open;
            out += "\\begin{longtable}{" + spec + "}\n";
            if (above && !caption.empty()) out += caption.substr(0, caption.size() - 1) + "\\\\\n";
            out += top() + head + "\\endfirsthead\n" + top() + head + "\\endhead\n" + bottom() + "\\endfoot\n";
            out += body;
            if (!above && !caption.empty()) out += caption.substr(0, caption.size() - 1) + "\\\\\n";
            out += "\\end{longtable}\n\\endgroup\n\n";
            return;
        }
        open_float(env, placement);
        if (above) out += caption;
        out += group_open + "\\begin{tabular}{" + spec + "}\n" + top() + head + body + bottom() + "\\end{tabular}\\par\n\\endgroup\n";
        if (!above) out += caption;
        close_float(env);
    }

    // "::: Name ... ::: Name": page setup of a format section.
    void section(const Node& n) {
        const std::string name = n.attr("class");
        const SectionStyle* s = st_.find_section(name);
        if (!s) {
            diags_.push_back({Diagnostic::Level::Warning, "unknown section '" + name + "' (define it under 'sections:')", {}, n.line});
            blocks(n.children);
            return;
        }
        const auto& m = s->margin;
        bool geometry = !m.top.empty() || !m.bottom.empty() || !m.left.empty() || !m.right.empty();
        const auto& base = st_.page.margin;
        auto pick = [](const std::string& v, const std::string& b) { return v.empty() ? b : v; };
        if (geometry)
            out += "\\newgeometry{top=" + pick(m.top, base.top) + ",bottom=" + pick(m.bottom, base.bottom) +
                   ",left=" + pick(m.left, base.left) + ",right=" + pick(m.right, base.right) + ",headheight=15pt}\n";
        if (s->orientation == "landscape") out += "\\begin{landscape}\n";
        if (s->columns > 1) {
            out += "\\begin{multicols}{" + std::to_string(s->columns) + "}\n";
            ++multicol_;
            multicol_cols_ = s->columns;
        }
        if (!s->font_size.empty()) out += "\\begingroup" + fontsize_cmd(s->font_size) + "\n";

        blocks(n.children);

        if (!s->font_size.empty()) out += "\\par\\endgroup\n";
        if (s->columns > 1) {
            out += "\\end{multicols}\n";
            --multicol_;
            multicol_cols_ = 1;
        }
        if (s->orientation == "landscape") out += "\\end{landscape}\n";
        if (geometry) out += "\\restoregeometry\n";
        out += "\n";
    }

    void bibliography(bool under_heading) {
        if (bib_emitted) return;
        bib_emitted = true;
        if (!facts.has_citations) {
            diags_.push_back({Diagnostic::Level::Warning, "the bibliography heading is empty because the document cites nothing"});
            return;
        }
        if (!opts || opts->bibliography.empty()) {
            diags_.push_back({Diagnostic::Level::Warning,
                              "the document cites sources but has no bibliography file; add 'bibliography: file.bib' to the front matter"});
            return;
        }
        std::string cmds = "\\bibliographystyle{" + plan.bib_style + "}\n\\bibliography{" + opts->bibliography + "}\n";
        // Under a {pustaka} heading the heading is the title, so natbib must not add its own.
        out += under_heading ? "\\begingroup\\renewcommand{\\bibsection}{}\n" + cmds + "\\endgroup\n\n" : cmds + "\n";
    }

    void cover() {
        if (st_.cover.empty()) return;
        out += "\\begin{titlepage}\n" + fill(st_.cover, "the cover", 0) + "\n\\end{titlepage}\n\n";
    }

    // Fills a template from the front matter and reports the keys it lacks.
    std::string fill(const std::string& tmpl, const std::string& what, size_t line) {
        std::vector<std::string> missing;
        auto tex = fill_template(tmpl, doc ? doc->meta : std::map<std::string, std::string>{}, st_.base_dir, &missing);
        std::string keys;
        for (auto& k : missing)
            if (keys.find("'" + k + "'") == std::string::npos) keys += (keys.empty() ? "'" : ", '") + k + "'";
        if (!keys.empty() && doc)
            diags_.push_back({Diagnostic::Level::Warning, what + " uses front matter " + keys + ", which the document does not set",
                              {}, line});
        return tex;
    }

    // ------------------------------------------------------------ inlines

    std::string inlines(const std::vector<Node>& kids) {
        std::string s;
        for (auto& k : kids) s += inl(k);
        return s;
    }

    std::string inl(const Node& n) {
        switch (n.type) {
            case NodeType::Text: return escape(n.text);
            case NodeType::SoftBreak: return "\n";
            case NodeType::LineBreak: return in_cell_ ? "\\newline " : "\\\\\n";
            case NodeType::Emph: return "\\emph{" + inlines(n.children) + "}";
            case NodeType::Strong: return "\\textbf{" + inlines(n.children) + "}";
            case NodeType::Strike: return "\\sout{" + inlines(n.children) + "}";
            case NodeType::Underline: return "\\uline{" + inlines(n.children) + "}";
            case NodeType::Code: return "\\texttt{" + escape(n.text) + "}";
            case NodeType::Math:
                return n.attr("display").empty() ? "$" + n.text + "$" : "\\[" + n.text + "\\]";
            case NodeType::Link: {
                std::string href = n.attr("href");
                std::string text = plain_text(n);
                if (text == href || "mailto:" + text == href) return "\\url{" + escape_url(text) + "}";
                return "\\href{" + escape_url(href) + "}{" + inlines(n.children) + "}";
            }
            case NodeType::Image: {
                std::string w = width_expr(n.attr("width"));
                return "\\includegraphics[" + (w.empty() ? std::string("height=1.2em") : "width=" + w) + "]{" + n.attr("src") + "}";
            }
            case NodeType::Citation: {
                std::string keys = n.attr("keys");
                std::string mode = n.attr("mode");
                std::string loc = n.attr("locator");
                std::string cmd = mode == "text" ? "\\citet" : mode == "year" ? "\\citeyearpar" : "\\citep";
                return cmd + (loc.empty() ? "" : "[" + escape(loc) + "]") + "{" + keys + "}";
            }
            case NodeType::CrossRef: {
                std::string id = n.attr("id");
                if (!ids.count(id))
                    diags_.push_back({Diagnostic::Level::Warning, "reference to unknown label '@" + id + "'", {}, n.line});
                std::string ref = "\\ref{" + id + "}";
                if (!st_.cross_reference.prefix) return ref;
                if (auto* l = st_.list_for_label(label_prefix(id))) return escape(l->prefix) + "~" + ref;
                return ref;
            }
            default:
                return inlines(n.children);
        }
    }

private:
    struct Scope {
        int level;
        std::vector<const TagStyle*> tags;
    };

    const Style& st_;
    Diagnostics& diags_;
    bool in_cell_ = false;
    int multicol_ = 0;
    int multicol_cols_ = 1;
    int renumbered_ = 0;
    size_t tex_line_ = 1;   // line the emitter is currently on, within the body
    size_t counted_ = 0;    // how much of `out` has been scanned for newlines
    size_t current_page_style_ = static_cast<size_t>(-1);
    std::vector<Scope> scopes_;
    std::array<std::string, 6> active_setup_;
    std::set<std::string> warned_tags_;
};

}  // namespace

std::string emit_inlines(const std::vector<Node>& inlines, const Style& style) {
    Diagnostics d;
    Emitter e(style, d);
    return e.inlines(inlines);
}

std::string emit_blocks(const Node& root, const Style& style, Diagnostics& diags) {
    Emitter e(style, diags);
    e.collect_ids(root);
    e.facts = collect_facts(root);
    e.resolve_bibliography();
    e.blocks(root.children);
    return e.out;
}

size_t markdown_line_for(const LineMap& map, size_t tex_line) {
    size_t md = 0;
    for (auto& [tex, markdown] : map) {
        if (tex > tex_line) break;
        md = markdown;
    }
    return md;
}

std::string emit_document(const Document& doc, const Style& style, const EmitOptions& opts, Diagnostics& diags,
                          LineMap* line_map) {
    Emitter e(style, diags);
    e.doc = &doc;
    e.opts = &opts;
    e.collect_ids(doc.root);
    e.facts = collect_facts(doc.root);
    e.resolve_bibliography();

    // The body is emitted first: it decides which page styles the preamble must define.
    std::string body;
    if (!e.facts.has_cover_heading && doc.meta.count("title")) e.cover();
    e.start_numbering();
    e.blocks(doc.root.children);
    if (e.facts.has_citations) e.bibliography(false);
    body.swap(e.out);

    std::string tex = build_preamble(style, doc.meta, e.plan);
    tex += "\n\\begin{document}\n";
    tex += build_document_start(style);
    tex += "\n";
    if (line_map) {
        // The body follows the preamble, so every recorded line shifts down by it.
        size_t offset = 0;
        for (char c : tex) offset += c == '\n';
        for (auto& [tex_line, md_line] : e.line_map) line_map->emplace_back(tex_line + offset, md_line);
    }
    tex += body;
    tex += "\\end{document}\n";
    return tex;
}

}  // namespace placi::latex
