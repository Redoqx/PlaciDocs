#include "core/latex/preamble.hpp"

#include <sstream>

#include "core/latex/escape.hpp"
#include "core/util.hpp"

namespace placi::latex {

namespace {

std::string paper_option(const std::string& size) {
    if (size == "A4") return "a4paper";
    if (size == "A5") return "a5paper";
    if (size == "B5") return "b5paper";
    if (size == "letter") return "letterpaper";
    if (size == "legal") return "legalpaper";
    if (size == "F4") return "paperwidth=215mm,paperheight=330mm";
    return "a4paper";
}

std::string babel_language(const std::string& lang) {
    if (lang == "english") return "english";
    if (lang == "malay") return "malay";
    return "indonesian";
}

// The TeX Gyre fonts ship inside the LaTeX bundle. XeTeX can only load bundle
// fonts by file name, so they are referenced as e.g. texgyretermes-regular.otf.
const char* gyre_file(const std::string& family) {
    static const std::pair<const char*, const char*> kGyre[] = {
        {"TeX Gyre Termes", "texgyretermes"}, {"TeX Gyre Heros", "texgyreheros"},
        {"TeX Gyre Cursor", "texgyrecursor"}, {"TeX Gyre Pagella", "texgyrepagella"},
        {"TeX Gyre Bonum", "texgyrebonum"},   {"TeX Gyre Schola", "texgyreschola"},
    };
    for (auto& [name, file] : kGyre)
        if (iequals(family, name)) return file;
    return nullptr;
}

// Fonts that are common on Windows but may be missing elsewhere get a metric-compatible TeX Gyre fallback.
const char* font_fallback(const std::string& family) {
    static const std::pair<const char*, const char*> kMap[] = {
        {"Times New Roman", "TeX Gyre Termes"}, {"Times", "TeX Gyre Termes"},
        {"Cambria", "TeX Gyre Termes"},        {"Arial", "TeX Gyre Heros"},
        {"Helvetica", "TeX Gyre Heros"},       {"Calibri", "TeX Gyre Heros"},
        {"Courier New", "TeX Gyre Cursor"},    {"Courier", "TeX Gyre Cursor"},
        {"Book Antiqua", "TeX Gyre Pagella"},  {"Palatino Linotype", "TeX Gyre Pagella"},
        {"Bookman Old Style", "TeX Gyre Bonum"}, {"Century Schoolbook", "TeX Gyre Schola"},
    };
    for (auto& [name, fb] : kMap)
        if (iequals(family, name)) return fb;
    return nullptr;
}

std::string gyre_cmd(const char* cmd, const char* file) {
    return std::string("\\") + cmd + "{" + file +
           "}[Extension=.otf,UprightFont=*-regular,BoldFont=*-bold,ItalicFont=*-italic,BoldItalicFont=*-bolditalic]";
}

// Missing fonts fall back to TeX Gyre, or to Latin Modern (the fontspec default).
void set_font(std::ostringstream& o, const char* cmd, const std::string& family) {
    if (family.empty()) return;
    if (auto* file = gyre_file(family)) {
        o << gyre_cmd(cmd, file) << "\n";
        return;
    }
    std::string fallback;
    if (auto* fb = font_fallback(family)) fallback = gyre_cmd(cmd, gyre_file(fb));
    o << "\\IfFontExistsTF{" << family << "}{\\" << cmd << "{" << family << "}}{" << fallback << "}\n";
}

std::string align_cmd(const std::string& align) {
    if (align == "center") return "\\filcenter";
    if (align == "right") return "\\filleft";
    return "\\filright";
}

std::string heading_format(const HeadingStyle& h) {
    std::string f = "\\normalfont";
    auto fs = fontsize_cmd(h.size);
    f += fs.empty() ? "\\normalsize" : fs;
    if (h.bold) f += "\\bfseries";
    if (h.italic) f += "\\itshape";
    f += align_cmd(h.align);
    return f;
}

std::string page_number_cmd(const std::string& pos) {
    if (pos.empty() || pos == "none") return "";
    bool top = pos.rfind("top", 0) == 0;
    char slot = pos.ends_with("left") ? 'L' : pos.ends_with("right") ? 'R' : 'C';
    return std::string(top ? "\\fancyhead[" : "\\fancyfoot[") + slot + "]{\\thepage}";
}

void page_style(std::ostringstream& o, const std::string& name, const std::string& pos) {
    o << "\\fancypagestyle{" << name << "}{\\fancyhf{}\\renewcommand{\\headrulewidth}{0pt}"
      << "\\renewcommand{\\footrulewidth}{0pt}" << page_number_cmd(pos) << "}\n";
}

std::string pagenumbering_style(const std::string& s) {
    if (s == "none") return "gobble";
    if (s == "alpha") return "alph";
    if (s == "Alpha") return "Alph";
    if (s == "roman" || s == "Roman" || s == "arabic") return s;
    return "arabic";
}

std::string page_style_name(size_t i) {
    // Page style names are letters only: placipna, placipnb, ...
    std::string suffix;
    do {
        suffix.insert(suffix.begin(), static_cast<char>('a' + i % 26));
        i /= 26;
    } while (i > 0);
    return "placipn" + suffix;
}

void caption_setup(std::ostringstream& o, const std::string& env, const CaptionStyle& c, const std::string& prefix) {
    std::string font = "placicapfont" + env;
    auto size = fontsize_cmd(c.font_size);
    o << "\\DeclareCaptionFont{" << font << "}{\\setstretch{1}" << (size.empty() ? "\\normalsize" : size) << "}\n";
    o << "\\DeclareCaptionLabelSeparator{placisep" << env << "}{" << escape(c.separator) << "}\n";
    std::string just = c.align == "left" ? "raggedright" : c.align == "justify" ? "justified" : "centering";
    o << "\\captionsetup[" << env << "]{name={" << escape(prefix) << "},labelsep=placisep" << env
      << ",justification=" << just << ",singlelinecheck=" << (c.align == "center" ? "true" : "false")
      << ",font=" << font << ",labelfont=" << (c.label_bold ? "bf" : "md") << ",position="
      << (c.position.empty() ? "below" : c.position) << ",skip=6pt}\n";
}

void counter_numbering(std::ostringstream& o, const std::string& counter, const std::string& tmpl) {
    o << "\\renewcommand{\\the" << counter << "}{" << numbering_expr(tmpl, counter) << "}\n";
    if (tmpl.find("{h1}") == std::string::npos) o << "\\counterwithout{" << counter << "}{chapter}\n";
    else o << "\\counterwithin*{" << counter << "}{chapter}\n";
}

std::string letters(size_t i) {
    std::string s;
    do {
        s.insert(s.begin(), static_cast<char>('a' + i % 26));
        i /= 26;
    } while (i > 0);
    return s;
}

}  // namespace

const ListEnv* DocPlan::env_for_list(const ListStyle* l) const {
    for (auto& e : lists)
        if (e.list == l) return &e;
    return nullptr;
}

size_t DocPlan::page_style_index(const PageNumberStyle& p) {
    for (size_t i = 0; i < page_styles.size(); ++i)
        if (page_styles[i] == p) return i;
    page_styles.push_back(p);
    return page_styles.size() - 1;
}

DocPlan make_plan(const Style& st) {
    DocPlan plan;
    bool have_fig = false, have_tbl = false, have_eq = false;
    size_t floats = 0;
    for (auto& [key, l] : st.lists) {
        ListEnv e;
        e.list = &l;
        if (l.kind == "figure" && !have_fig) {
            have_fig = true;
            e.env = e.counter = "figure";
            e.ext = "lof";
        } else if (l.kind == "table" && !have_tbl) {
            have_tbl = true;
            e.env = e.counter = "table";
            e.ext = "lot";
        } else if (l.kind == "equation" && !have_eq) {
            have_eq = true;
            e.env = e.counter = "equation";
            e.ext = "loe";
        } else {
            // A user list: its own float environment, counter and list file.
            std::string s = letters(floats++);
            e.env = e.counter = "placifloat" + s;
            e.ext = "lop" + s;
        }
        plan.lists.push_back(e);
    }
    plan.bib_style = st.bibliography.style;
    plan.citation = st.bibliography.citation;
    plan.page_style_index(st.page_numbering);  // index 0 = the main numbering
    return plan;
}

std::string heading_setup(int level, const HeadingStyle& h, bool chapter_number_follows_label) {
    std::ostringstream o;
    std::string cmd = section_command(level);
    o << "\\renewcommand{\\the" << cmd << "}{" << numbering_expr(h.numbering, cmd) << "}";
    if (level == 1)
        o << "\\renewcommand{\\placichapnum}{" << (chapter_number_follows_label ? "\\thechapter" : "\\arabic{chapter}") << "}";
    o << "\n";
    std::string shape = h.label_position == "above" ? "display" : "hang";
    std::string label = h.numbered ? label_expr(h.label, "\\the" + cmd) : "";
    if (h.uppercase && !label.empty()) label = "\\MakeUppercase{" + label + "}";
    std::string sep = h.numbered ? h.label_sep : "0pt";
    if (shape == "display" && h.numbered) sep = "0pt";  // label and title on consecutive lines
    o << "\\titleformat{\\" << cmd << "}[" << shape << "]{" << heading_format(h) << "}{" << label << "}{" << sep
      << "}{" << (h.uppercase ? "\\MakeUppercase" : "") << "}\n";
    o << "\\titlespacing*{\\" << cmd << "}{0pt}{" << h.space_before << "}{" << h.space_after << "}\n";
    return o.str();
}

std::string page_numbering_switch(size_t i, const PageNumberStyle& p, const PageNumberStyle* previous) {
    std::string name = page_style_name(i);
    std::string restart =
        previous && previous->style == p.style ? std::string() : "\\pagenumbering{" + pagenumbering_style(p.style) + "}";
    return "\\clearpage" + restart + "\\pagestyle{" + name + "}\\placisetplain{" + name + "first}\n";
}

std::string fill_template(const std::string& tmpl, const std::map<std::string, std::string>& meta,
                          const std::string& style_dir, std::vector<std::string>* missing) {
    std::string tex;
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl.compare(i, 2, "{{") == 0) {
            size_t close = tmpl.find("}}", i + 2);
            if (close != std::string::npos) {
                std::string key(trim(std::string_view(tmpl).substr(i + 2, close - i - 2)));
                if (key == "style_dir") {
                    tex += style_dir;
                } else if (auto it = meta.find(key); it != meta.end() && !it->second.empty()) {
                    tex += escape(it->second);
                } else if (missing) {
                    missing->push_back(key);
                }
                i = close + 2;
                continue;
            }
        }
        tex += tmpl[i++];
    }
    return tex;
}

std::string build_preamble(const Style& st, const std::map<std::string, std::string>& meta, const DocPlan& plan) {
    std::ostringstream o;

    double body_pt = to_points(st.font.size).value_or(12);
    int class_pt = body_pt == 10 || body_pt == 11 || body_pt == 12 ? static_cast<int>(body_pt) : 12;

    o << "% Generated by PlaciDocs from style '" << st.name << "'. Do not edit; edit the style instead.\n";
    o << "\\documentclass[" << class_pt << "pt," << (st.page.twoside ? "twoside,openany" : "oneside") << "]{report}\n";
    if (class_pt != body_pt) o << "\\usepackage{scrextend}\\changefontsizes{" << format_number(body_pt) << "pt}\n";

    o << "\n% --- page\n";
    const auto& m = st.page.margin;
    o << "\\usepackage[" << paper_option(st.page.size) << ",top=" << m.top << ",bottom=" << m.bottom
      << ",left=" << m.left << ",right=" << m.right << ",headheight=15pt]{geometry}\n";

    o << "\n% --- fonts & language\n";
    o << "\\usepackage{amsmath,amssymb}\n\\usepackage{fontspec}\n";
    set_font(o, "setmainfont", st.font.family);
    set_font(o, "setsansfont", st.font.sans);
    set_font(o, "setmonofont", st.font.mono);
    o << "\\usepackage[" << babel_language(st.language) << "]{babel}\n";

    o << "\n% --- packages\n";
    o << "\\usepackage{etoolbox,setspace,ragged2e,graphicx,float,array,longtable,booktabs,pdflscape,fancyvrb,enumitem,multicol}\n";
    o << "\\usepackage[table]{xcolor}\n\\usepackage[normalem]{ulem}\n";
    o << "\\usepackage{titlesec,fancyhdr,caption,newfloat}\n";
    o << "\\usepackage[" << (plan.citation == "numeric" ? "numbers,square" : "round") << "]{natbib}\n";

    o << "\n% --- paragraphs\n";
    double ls = st.paragraph.line_spacing;
    if (ls == 1.0) o << "\\singlespacing\n";
    else if (ls == 1.5) o << "\\onehalfspacing\n";
    else if (ls == 2.0) o << "\\doublespacing\n";
    else o << "\\setstretch{" << format_number(ls) << "}\n";
    o << "\\setlength{\\parindent}{" << st.paragraph.indent << "}\n";
    o << "\\setlength{\\parskip}{\\dimexpr " << st.paragraph.space_before << "+" << st.paragraph.space_after << "\\relax}\n";
    if (st.paragraph.indent_first) o << "\\usepackage{indentfirst}\n";
    o << "\\setlength{\\RaggedRightParindent}{" << st.paragraph.indent << "}\n";
    o << "\\setlength{\\JustifyingParindent}{" << st.paragraph.indent << "}\n";
    if (st.paragraph.align == "left") o << "\\AtBeginDocument{\\RaggedRight}\n";
    else if (st.paragraph.align == "right") o << "\\AtBeginDocument{\\RaggedLeft}\n";
    else if (st.paragraph.align == "center") o << "\\AtBeginDocument{\\Centering}\n";
    o << "\\setlength{\\emergencystretch}{3em}\n";
    o << "\\setlist{itemsep=0pt,parsep=0pt,topsep=4pt}\n";

    o << "\n% --- headings\n";
    o << "\\setcounter{secnumdepth}{5}\n\\setcounter{tocdepth}{" << (st.contents.depth - 1) << "}\n";
    o << "\\newcommand{\\placichapnum}{\\arabic{chapter}}\n";
    for (int lvl = 1; lvl <= 6; ++lvl) o << heading_setup(lvl, st.headings[lvl - 1], false);
    if (!st.headings[0].new_page)
        o << "\\makeatletter\\patchcmd{\\chapter}{\\if@openright\\cleardoublepage\\else\\clearpage\\fi}{}{}{}\\makeatother\n";

    o << "\n% --- page numbers\n";
    for (size_t i = 0; i < plan.page_styles.size(); ++i) {
        const auto& p = plan.page_styles[i];
        page_style(o, page_style_name(i), p.position);
        page_style(o, page_style_name(i) + "first", p.first());
    }
    o << "\\makeatletter\\newcommand{\\placisetplain}[1]{\\expandafter\\let\\expandafter\\ps@plain\\csname ps@#1\\endcsname}\\makeatother\n";

    o << "\n% --- lists (figures, tables, equations, user lists)\n";
    o << "\\makeatletter\n\\newcommand{\\placilistof}[1]{\\@starttoc{#1}}\n";
    o << "\\newcommand*{\\l@equation}{\\@dottedtocline{1}{0em}{3.5em}}\n";
    // Equation numbers are written exactly as the list's numbering template says (it may include the parentheses).
    o << "\\renewcommand{\\tagform@}[1]{\\maketag@@@{\\ignorespaces#1\\unskip\\@@italiccorr}}\n\\makeatother\n";
    for (auto& e : plan.lists) {
        const ListStyle& l = *e.list;
        if (e.env == "equation") {
            counter_numbering(o, "equation", l.numbering);
            continue;
        }
        if (e.env != "figure" && e.env != "table")
            o << "\\DeclareFloatingEnvironment[fileext=" << e.ext << ",listname={" << escape(l.title) << "},name={"
              << escape(l.prefix) << "},placement=H]{" << e.env << "}\n";
        const CaptionStyle& cap = e.env == "table" ? st.table.caption : st.figure.caption;
        caption_setup(o, e.env, cap, l.prefix);
        counter_numbering(o, e.counter, l.numbering);
    }
    o << "\\newcommand{\\placitablefont}{" << fontsize_cmd(st.table.font_size) << "}\n";

    o << "\n% --- bibliography\n";
    o << "\\renewcommand{\\bibsection}{\\chapter*{\\bibname}"
      << (st.bibliography.in_toc ? "\\phantomsection\\addcontentsline{toc}{chapter}{\\bibname}" : "") << "}\n";

    if (!st.preamble.empty()) o << "\n% --- style preamble\n" << st.preamble << "\n";

    o << "\n% --- links (keep last)\n";
    o << "\\usepackage[hidelinks,unicode]{hyperref}\n";
    auto title = meta.find("title");
    auto author = meta.find("author");
    o << "\\hypersetup{pdftitle={" << (title != meta.end() ? escape(title->second) : "") << "},pdfauthor={"
      << (author != meta.end() ? escape(author->second) : "") << "}}\n";
    return o.str();
}

std::string build_document_start(const Style& st) {
    // babel installs its caption names at \begin{document}; override them afterwards.
    return "\\renewcommand{\\bibname}{" + escape(st.bibliography.title) + "}\n";
}

}  // namespace placi::latex
