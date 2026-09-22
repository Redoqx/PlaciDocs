#pragma once
// Line-level pass that runs before md4c. It handles the block syntax md4c
// does not know about:
//   * a YAML front matter block at the very top (--- ... ---)
//   * format sections:  ::: Name  ...  ::: Name   (the name is required at both ends)
//   * page breaks:      a line holding only {halaman-baru} / {pagebreak}
//   * escaped tag delimiters:  \{  \}
// Sections and page breaks become sentinel paragraphs that the AST builder
// turns into DivMarker nodes. Fenced code blocks are left untouched.

#include <string>
#include <string_view>
#include <vector>

#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi {

// Sentinels live in the Unicode private use area, which never occurs in
// normal text and survives md4c unchanged.
inline constexpr std::string_view kSentinel = "\xEE\x80\x80";      // U+E000, wraps markers
inline constexpr std::string_view kEscapedOpen = "\xEE\x80\x81";   // U+E001, stands for "\{"
inline constexpr std::string_view kEscapedClose = "\xEE\x80\x82";  // U+E002, stands for "\}"

struct Preprocessed {
    std::string markdown;         // what md4c parses
    std::string front_matter;     // raw YAML (empty if none)
    size_t front_matter_line = 0;
    std::vector<size_t> line_map; // markdown line index (0-based) -> source line (1-based)
};

// Only the front matter, so the style (and its syntax) can be resolved before parsing.
std::string extract_front_matter(std::string_view source, size_t* first_line = nullptr);

Preprocessed preprocess(std::string_view source, const SyntaxStyle& syntax, Diagnostics& diags,
                        const std::string& filename = {});

}  // namespace placi
