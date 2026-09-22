#pragma once
// AST transforms that turn the raw md4c tree into PlaciDocs semantics.

#include <string>
#include <vector>

#include "core/ast.hpp"
#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi {

// What the parser needs to know from the style.
struct ParseOptions {
    SyntaxStyle syntax;
    std::vector<std::string> ref_prefixes{"fig", "tbl", "eq", "sec", "lst"};  // @prefix:x is a cross reference

    static ParseOptions from_style(const Style& st);
};

void fold_divs(Node& root, Diagnostics& diags);                     // DivMarkers -> Div / PageBreak
void apply_heading_tags(Node& root, const SyntaxStyle& sx);         // "Judul {pembuka - #sec:x}"
void build_figures(Node& root, const SyntaxStyle& sx);              // lone image paragraph -> Figure
void attach_table_captions(Node& root, const SyntaxStyle& sx);      // "Tabel: ..." paragraph -> Table.caption
void build_display_math(Node& root, const SyntaxStyle& sx);         // $$..$$ {#eq:x} and ```math
void resolve_citations(Node& root, const std::vector<std::string>& ref_prefixes);  // [@key], @key, @fig:x
void restore_escapes(Node& root, const SyntaxStyle& sx);            // \{ -> {

struct DocumentFacts {
    bool has_citations = false;
    bool has_cover_heading = false;
    const Node* bibliography_heading = nullptr;
};
DocumentFacts collect_facts(const Node& root);

// Runs all of the above in order.
void run_passes(Node& root, Diagnostics& diags, const ParseOptions& opts);

}  // namespace placi
