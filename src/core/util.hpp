#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace placi {

namespace fs = std::filesystem;

// Error with an optional source location; thrown for anything the user must fix.
struct PlaciError : std::runtime_error {
    std::string file;
    size_t line = 0;  // 1-based, 0 = unknown
    PlaciError(std::string msg, std::string file_ = {}, size_t line_ = 0)
        : std::runtime_error(std::move(msg)), file(std::move(file_)), line(line_) {}
    std::string describe() const;
};

struct Diagnostic {
    enum class Level { Warning, Error } level = Level::Warning;
    std::string message;
    std::string file;
    size_t line = 0;

    Diagnostic(Level lv, std::string msg, std::string file_ = {}, size_t line_ = 0)
        : level(lv), message(std::move(msg)), file(std::move(file_)), line(line_) {}
    std::string describe() const;
};
using Diagnostics = std::vector<Diagnostic>;

std::string read_file(const fs::path& p);
void write_file(const fs::path& p, std::string_view content);

// UTF-8 aware conversions for std::filesystem on Windows.
fs::path u8path(std::string_view s);
std::string path_str(const fs::path& p);  // UTF-8, forward slashes

std::string_view trim(std::string_view s);
std::vector<std::string_view> split_lines(std::string_view s);
bool iequals(std::string_view a, std::string_view b);
std::string to_lower(std::string_view s);

// Parses the body of a tag / attribute block (without delimiters):
//   pembuka - #fig:x .wide width=70% caption="a b"
// "-" is shorthand for ".unnumbered"; bare words are tag names.
struct AttrBlock {
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> words;
    std::vector<std::pair<std::string, std::string>> kv;
};
std::optional<AttrBlock> parse_attr_block(std::string_view body);

// If `s` ends with open...close (optionally followed by whitespace), splits it
// off. Returns the block body and shortens `s` accordingly.
std::optional<std::string> take_trailing_attrs(std::string& s, std::string_view open = "{", std::string_view close = "}");

// If `s` starts with open...close, splits it off (used right after images).
std::optional<std::string> take_leading_attrs(std::string& s, std::string_view open = "{", std::string_view close = "}");

// Parses a TeX length/number such as "12pt" into points. Returns nullopt for
// relative units it cannot resolve.
std::optional<double> to_points(std::string_view len);
bool is_tex_length(std::string_view len);

std::string format_number(double v);  // 1.5 -> "1.5", 2 -> "2"

}  // namespace placi
