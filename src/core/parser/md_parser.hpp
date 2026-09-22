#pragma once

#include <string_view>

#include "core/ast.hpp"
#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi {

// Parses PlaciDocs Markdown into a raw AST (DivMarkers not yet folded,
// citations not yet recognised). Use `parse_document` from pipeline.hpp for
// the fully processed tree.
Document parse_markdown(std::string_view source, const SyntaxStyle& syntax, Diagnostics& diags,
                        const std::string& filename = {});

}  // namespace placi
