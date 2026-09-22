#include "core/parser/preprocess.hpp"

#include <cctype>

namespace placi {

namespace {

struct Fence {
    char ch = 0;
    size_t len = 0;
};

// Returns the fence of a ``` / ~~~ line, if it is one.
Fence code_fence(std::string_view line) {
    size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    if (i >= line.size() || (line[i] != '`' && line[i] != '~')) return {};
    char ch = line[i];
    size_t n = 0;
    while (i + n < line.size() && line[i + n] == ch) ++n;
    if (n < 3) return {};
    return {ch, n};
}

bool is_name_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'; }

// "::: Name" -> true with `name` filled; "::: " alone -> true with empty name.
bool section_fence(std::string_view line, std::string& name) {
    auto t = trim(line);
    size_t n = 0;
    while (n < t.size() && t[n] == ':') ++n;
    if (n < 3) return false;
    auto rest = trim(t.substr(n));
    for (char c : rest)
        if (!is_name_char(c)) return false;
    name = std::string(rest);
    return true;
}

// A line holding only a page-break tag, e.g. "{halaman-baru}".
bool page_break_line(std::string_view line, const SyntaxStyle& sx) {
    auto t = trim(line);
    if (t.size() <= sx.tag_open.size() + sx.tag_close.size()) return false;
    if (t.substr(0, sx.tag_open.size()) != sx.tag_open) return false;
    if (t.substr(t.size() - sx.tag_close.size()) != sx.tag_close) return false;
    auto word = trim(t.substr(sx.tag_open.size(), t.size() - sx.tag_open.size() - sx.tag_close.size()));
    return word == "halaman-baru" || word == "pagebreak";
}

std::string escape_delimiters(std::string_view line, const SyntaxStyle& sx) {
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            auto rest = line.substr(i + 1);
            if (rest.substr(0, sx.tag_open.size()) == sx.tag_open) {
                out += kEscapedOpen;
                i += sx.tag_open.size();
                continue;
            }
            if (rest.substr(0, sx.tag_close.size()) == sx.tag_close) {
                out += kEscapedClose;
                i += sx.tag_close.size();
                continue;
            }
        }
        out += line[i];
    }
    return out;
}

}  // namespace

std::string extract_front_matter(std::string_view source, size_t* first_line) {
    auto lines = split_lines(source);
    if (lines.empty() || trim(lines[0]) != "---") return {};
    for (size_t j = 1; j < lines.size(); ++j) {
        auto t = trim(lines[j]);
        if (t == "---" || t == "...") {
            std::string fm;
            for (size_t k = 1; k < j; ++k) {
                fm += lines[k];
                fm += '\n';
            }
            if (first_line) *first_line = 2;
            return fm;
        }
    }
    return {};
}

Preprocessed preprocess(std::string_view source, const SyntaxStyle& syntax, Diagnostics& diags,
                        const std::string& filename) {
    Preprocessed out;
    auto lines = split_lines(source);
    out.markdown.reserve(source.size() + 64);

    auto emit = [&](std::string_view text, size_t src_line) {
        out.markdown += text;
        out.markdown += '\n';
        out.line_map.push_back(src_line);
    };

    size_t i = 0;
    out.front_matter = extract_front_matter(source, &out.front_matter_line);
    if (out.front_matter_line) {
        // Skip the front matter but keep one (blank) markdown line per source line.
        while (i < lines.size()) {
            emit("", i + 1);
            if (i > 0 && (trim(lines[i]) == "---" || trim(lines[i]) == "...")) {
                ++i;
                break;
            }
            ++i;
        }
    }

    struct Open {
        std::string name;
        size_t line;
    };
    std::vector<Open> open_sections;
    Fence open_code;

    auto marker = [&](const std::string& body, size_t src_line) {
        // Blank lines around the marker force md4c to see it as its own paragraph.
        emit("", src_line);
        emit(std::string(kSentinel) + body + "|" + std::to_string(src_line) + std::string(kSentinel), src_line);
        emit("", src_line);
    };

    for (; i < lines.size(); ++i) {
        auto line = lines[i];
        size_t src = i + 1;
        if (open_code.ch) {
            Fence f = code_fence(line);
            if (f.ch == open_code.ch && f.len >= open_code.len && trim(line).size() == f.len) open_code = {};
            emit(line, src);
            continue;
        }
        if (Fence f = code_fence(line); f.ch) {
            open_code = f;
            emit(line, src);
            continue;
        }
        if (std::string name; section_fence(line, name)) {
            if (name.empty()) {
                diags.push_back({Diagnostic::Level::Error,
                                 "':::' needs the section name at both ends, e.g. '::: Landscape' ... '::: Landscape'",
                                 filename, src});
                continue;
            }
            bool is_open = false;
            for (auto& o : open_sections) is_open |= o.name == name;
            if (!is_open) {
                open_sections.push_back({name, src});
                marker("open " + name, src);
                continue;
            }
            if (open_sections.back().name != name)
                diags.push_back({Diagnostic::Level::Error,
                                 "'::: " + name + "' closes a section that is not the innermost one (close '::: " +
                                     open_sections.back().name + "' first)",
                                 filename, src});
            // Close inner sections too, so the tree stays well formed.
            while (!open_sections.empty()) {
                auto closing = open_sections.back().name;
                open_sections.pop_back();
                marker("close " + closing, src);
                if (closing == name) break;
            }
            continue;
        }
        if (page_break_line(line, syntax)) {
            marker("pagebreak", src);
            continue;
        }
        emit(escape_delimiters(line, syntax), src);
    }
    while (!open_sections.empty()) {
        auto o = open_sections.back();
        open_sections.pop_back();
        diags.push_back({Diagnostic::Level::Error,
                         "'::: " + o.name + "' is never closed; add '::: " + o.name + "' where the section ends",
                         filename, o.line});
        marker("close " + o.name, lines.size());  // keep the tree well formed
    }
    return out;
}

}  // namespace placi
