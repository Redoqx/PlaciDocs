#pragma once

#include <string>

#include "core/ast.hpp"
#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi::latex {

struct EmitOptions {
    std::string bibliography;  // path of the .bib file as TeX should see it (without extension)
};

// Complete .tex source: preamble + body.
std::string emit_document(const Document& doc, const Style& style, const EmitOptions& opts, Diagnostics& diags);

// Body only, for a subtree (used by tests and, later, by incremental preview).
std::string emit_blocks(const Node& root, const Style& style, Diagnostics& diags);
std::string emit_inlines(const std::vector<Node>& inlines, const Style& style);

}  // namespace placi::latex
