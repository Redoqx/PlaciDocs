#pragma once
// High-level entry points used by the CLI (and later the GUI).

#include <optional>
#include <string>

#include "core/ast.hpp"
#include "core/compile/tectonic.hpp"
#include "core/passes/passes.hpp"
#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi {

// Markdown source -> fully processed AST (passes applied, rules not yet).
Document parse_document(std::string_view source, Diagnostics& diags, const std::string& filename = {},
                        const ParseOptions& opts = {});

// Directories that hold the built-in styles.
std::vector<fs::path> builtin_style_dirs();

// Style chosen by --style, else the front matter `style:`, else "default".
Style resolve_style(const std::map<std::string, std::string>& meta, const fs::path& doc_dir,
                    const std::optional<std::string>& override_style, Diagnostics& diags);

struct TexOutput {
    Document doc;
    Style style;
    std::string tex;
};

// Produces the LaTeX source of `input`. When `source` is given it is used
// instead of the file contents (e.g. an editor buffer that is not saved yet);
// `input` still decides where images, the .bib file and relative styles live.
TexOutput generate_tex(const fs::path& input, const std::optional<std::string>& style, Diagnostics& diags,
                       const std::string* source = nullptr);

struct BuildOptions {
    fs::path input;
    std::optional<std::string> style;
    std::optional<fs::path> output;   // default: <input stem>.pdf next to the input
    bool online = false;              // allow Tectonic to download packages missing from texcache/
    std::optional<std::string> source;  // unsaved buffer to build instead of the file
    std::string job_name;             // name of the .tex/.pdf in .placi/ (default: input stem)
};

struct BuildResult {
    CompileResult compile;
    fs::path pdf;
    fs::path tex;
};

BuildResult build_pdf(const BuildOptions& opts, Diagnostics& diags);

}  // namespace placi
