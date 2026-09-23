#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/ast.hpp"
#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi::latex {

struct EmitOptions {
    std::string bibliography;  // path of the .bib file as TeX should see it (without extension)
};

// Where each part of the generated .tex came from: (1-based .tex line, 1-based
// Markdown line), ascending. Lets LaTeX errors be reported at the line the
// author actually wrote.
using LineMap = std::vector<std::pair<size_t, size_t>>;

// The Markdown line that produced `tex_line`, or 0 if unknown.
size_t markdown_line_for(const LineMap& map, size_t tex_line);

// Complete .tex source: preamble + body.
std::string emit_document(const Document& doc, const Style& style, const EmitOptions& opts, Diagnostics& diags,
                          LineMap* line_map = nullptr);

// Body only, for a subtree (used by tests and, later, by incremental preview).
std::string emit_blocks(const Node& root, const Style& style, Diagnostics& diags);
std::string emit_inlines(const std::vector<Node>& inlines, const Style& style);

}  // namespace placi::latex
