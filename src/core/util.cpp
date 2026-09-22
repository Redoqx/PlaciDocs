#include "core/util.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace placi {

std::string PlaciError::describe() const {
    std::string out;
    if (!file.empty()) {
        out += file;
        if (line) out += ":" + std::to_string(line);
        out += ": ";
    }
    return out + what();
}

std::string Diagnostic::describe() const {
    std::string out;
    if (!file.empty()) {
        out += file;
        if (line) out += ":" + std::to_string(line);
        out += ": ";
    }
    out += level == Level::Error ? "error: " : "warning: ";
    return out + message;
}

fs::path u8path(std::string_view s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string path_str(const fs::path& p) {
    auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw PlaciError("cannot open file", path_str(p));
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);  // UTF-8 BOM
    return s;
}

void write_file(const fs::path& p, std::string_view content) {
    if (p.has_parent_path()) fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    if (!out) throw PlaciError("cannot write file", path_str(p));
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string_view trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::vector<std::string_view> split_lines(std::string_view s) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string_view::npos) {
            if (start < s.size()) lines.push_back(s.substr(start));
            break;
        }
        size_t end = nl;
        if (end > start && s[end - 1] == '\r') --end;
        lines.push_back(s.substr(start, end - start));
        start = nl + 1;
    }
    return lines;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

std::string to_lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::optional<AttrBlock> parse_attr_block(std::string_view body) {
    AttrBlock ab;
    size_t i = 0;
    auto skip_ws = [&] { while (i < body.size() && std::isspace(static_cast<unsigned char>(body[i]))) ++i; };
    auto read_word = [&] {
        size_t b = i;
        while (i < body.size() && !std::isspace(static_cast<unsigned char>(body[i])) && body[i] != '=') ++i;
        return std::string(body.substr(b, i - b));
    };
    skip_ws();
    if (i == body.size()) return std::nullopt;
    while (i < body.size()) {
        if (body[i] == '#') {
            ++i;
            ab.id = read_word();
            if (ab.id.empty()) return std::nullopt;
        } else if (body[i] == '.') {
            ++i;
            auto c = read_word();
            if (c.empty()) return std::nullopt;
            ab.classes.push_back(c);
        } else if (body[i] == '-' && (i + 1 == body.size() || std::isspace(static_cast<unsigned char>(body[i + 1])))) {
            ++i;
            ab.classes.push_back("unnumbered");
        } else {
            auto key = read_word();
            if (key.empty()) return std::nullopt;
            if (i >= body.size() || body[i] != '=') {
                ab.words.push_back(std::move(key));  // a tag name
                skip_ws();
                continue;
            }
            ++i;  // '='
            std::string val;
            if (i < body.size() && body[i] == '"') {
                ++i;
                size_t b = i;
                while (i < body.size() && body[i] != '"') ++i;
                if (i == body.size()) return std::nullopt;
                val = std::string(body.substr(b, i - b));
                ++i;
            } else {
                size_t b = i;
                while (i < body.size() && !std::isspace(static_cast<unsigned char>(body[i]))) ++i;
                val = std::string(body.substr(b, i - b));
            }
            ab.kv.emplace_back(std::move(key), std::move(val));
        }
        skip_ws();
    }
    return ab;
}

std::optional<std::string> take_trailing_attrs(std::string& s, std::string_view open, std::string_view close) {
    auto t = trim(s);
    if (t.size() < open.size() + close.size() || t.substr(t.size() - close.size()) != close) return std::nullopt;
    size_t o = t.rfind(open, t.size() - close.size() - (open == close ? 1 : 0));
    if (o == std::string_view::npos) return std::nullopt;
    size_t body_start = o + open.size();
    if (body_start > t.size() - close.size()) return std::nullopt;
    std::string body(t.substr(body_start, t.size() - close.size() - body_start));
    if (!parse_attr_block(body)) return std::nullopt;
    size_t cut = static_cast<size_t>(t.data() - s.data()) + o;
    s.erase(cut);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return body;
}

std::optional<std::string> take_leading_attrs(std::string& s, std::string_view open, std::string_view close) {
    if (s.compare(0, open.size(), open) != 0) return std::nullopt;
    size_t c = s.find(close, open.size());
    if (c == std::string::npos) return std::nullopt;
    std::string body = s.substr(open.size(), c - open.size());
    if (!parse_attr_block(body)) return std::nullopt;
    s.erase(0, c + close.size());
    return body;
}

namespace {
struct UnitFactor { std::string_view unit; double pt; };
constexpr UnitFactor kUnits[] = {
    {"pt", 1.0}, {"bp", 72.27 / 72.0}, {"mm", 72.27 / 25.4}, {"cm", 72.27 / 2.54},
    {"in", 72.27}, {"pc", 12.0}, {"dd", 1238.0 / 1157.0}, {"cc", 12.0 * 1238.0 / 1157.0}, {"sp", 1.0 / 65536.0},
};
constexpr std::string_view kRelUnits[] = {"em", "ex"};

bool split_number(std::string_view s, double& value, std::string_view& unit) {
    s = trim(s);
    size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
    while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
    if (i == 0) return false;
    auto num = std::string(s.substr(0, i));
    try {
        size_t used = 0;
        value = std::stod(num, &used);
        if (used != num.size()) return false;
    } catch (...) {
        return false;
    }
    unit = trim(s.substr(i));
    return true;
}
}  // namespace

std::optional<double> to_points(std::string_view len) {
    double v;
    std::string_view unit;
    if (!split_number(len, v, unit)) return std::nullopt;
    for (auto& u : kUnits)
        if (unit == u.unit) return v * u.pt;
    return std::nullopt;
}

bool is_tex_length(std::string_view len) {
    double v;
    std::string_view unit;
    if (!split_number(len, v, unit)) return false;
    if (v == 0 && unit.empty()) return true;
    for (auto& u : kUnits)
        if (unit == u.unit) return true;
    for (auto& u : kRelUnits)
        if (unit == u) return true;
    return false;
}

std::string format_number(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

}  // namespace placi
