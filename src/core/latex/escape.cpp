#include "core/latex/escape.hpp"

#include "core/util.hpp"

namespace placi::latex {

std::string escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + s.size() / 8);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\textbackslash{}"; break;
            case '{': out += "\\{"; break;
            case '}': out += "\\}"; break;
            case '$': out += "\\$"; break;
            case '&': out += "\\&"; break;
            case '#': out += "\\#"; break;
            case '%': out += "\\%"; break;
            case '_': out += "\\_"; break;
            case '^': out += "\\^{}"; break;
            case '~': out += "\\textasciitilde{}"; break;
            default: out += c;
        }
    }
    return out;
}

std::string escape_url(std::string_view s) {
    std::string out;
    for (char c : s) {
        if (c == '%' || c == '#' || c == '\\' || c == '{' || c == '}') out += '\\';
        out += c;
    }
    return out;
}

const char* section_command(int level) {
    static constexpr const char* kCmds[] = {"chapter", "section", "subsection", "subsubsection", "paragraph", "subparagraph"};
    if (level < 1) level = 1;
    if (level > 6) level = 6;
    return kCmds[level - 1];
}

std::string numbering_expr(std::string_view tmpl, std::string_view counter) {
    std::string out;
    std::string literal;
    auto flush = [&] {
        out += escape(literal);
        literal.clear();
    };
    std::string cnt(counter);
    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{') {
            size_t close = tmpl.find('}', i);
            if (close != std::string_view::npos) {
                auto tok = tmpl.substr(i + 1, close - i - 1);
                std::string expr;
                if (tok == "n") expr = "\\arabic{" + cnt + "}";
                else if (tok == "roman") expr = "\\roman{" + cnt + "}";
                else if (tok == "Roman") expr = "\\Roman{" + cnt + "}";
                else if (tok == "alpha") expr = "\\alph{" + cnt + "}";
                else if (tok == "Alpha") expr = "\\Alph{" + cnt + "}";
                else if (tok == "h1")
                    expr = "\\placichapnum{}";  // arabic, or the appendix letter inside ::: appendix
                else if (tok.size() == 2 && tok[0] == 'h' && tok[1] >= '2' && tok[1] <= '6')
                    expr = std::string("\\arabic{") + section_command(tok[1] - '0') + "}";
                if (!expr.empty()) {
                    flush();
                    out += expr;
                    i = close;
                    continue;
                }
            }
        }
        literal += tmpl[i];
    }
    flush();
    return out;
}

std::string label_expr(std::string_view tmpl, std::string_view number) {
    std::string out;
    size_t pos = 0;
    while (true) {
        size_t hit = tmpl.find("{num}", pos);
        out += escape(tmpl.substr(pos, hit == std::string_view::npos ? std::string_view::npos : hit - pos));
        if (hit == std::string_view::npos) break;
        out += number;
        pos = hit + 5;
    }
    return out;
}

std::string fontsize_cmd(std::string_view size) {
    auto pt = to_points(size);
    if (!pt) return {};
    return "\\fontsize{" + format_number(*pt) + "pt}{" + format_number(*pt * 1.2) + "pt}\\selectfont";
}

}  // namespace placi::latex
