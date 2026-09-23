#include "core/pipeline.hpp"

#include <cctype>
#include <fstream>
#include <functional>

#include "core/compile/process.hpp"
#include "core/latex/emitter.hpp"
#include "core/parser/md_parser.hpp"
#include "core/parser/preprocess.hpp"
#include "core/passes/passes.hpp"
#include "core/rules/rule_engine.hpp"
#include "core/style/style_loader.hpp"

namespace placi {

Document parse_document(std::string_view source, Diagnostics& diags, const std::string& filename,
                        const ParseOptions& opts) {
    Document doc = parse_markdown(source, opts.syntax, diags, filename);
    run_passes(doc.root, diags, opts);
    for (auto& d : diags)
        if (d.file.empty()) d.file = filename;
    return doc;
}

std::vector<fs::path> builtin_style_dirs() {
    auto dir = executable_dir();
    std::vector<fs::path> dirs;
    for (auto& p : {dir / "styles", dir.parent_path() / "styles", dir.parent_path().parent_path() / "styles"}) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) dirs.push_back(p);
    }
    return dirs;
}

Style resolve_style(const std::map<std::string, std::string>& meta, const fs::path& doc_dir,
                    const std::optional<std::string>& override_style, Diagnostics& diags) {
    std::string name = override_style.value_or("");
    if (name.empty()) {
        auto it = meta.find("style");
        name = it != meta.end() ? it->second : "default";
    }
    auto dirs = builtin_style_dirs();
    auto path = find_style(name, doc_dir, dirs);
    if (!path) {
        if (name == "default") return Style{};
        throw PlaciError("style '" + name + "' not found (looked next to the document and in the built-in styles)");
    }
    return load_style(*path, diags, dirs);
}

namespace {

// Marks figures whose image file does not exist, so the build still succeeds.
void check_images(Node& n, const fs::path& doc_dir, Diagnostics& diags, const std::string& file) {
    if (n.type == NodeType::Figure || n.type == NodeType::Image) {
        auto src = n.attr("src");
        std::error_code ec;
        if (src.find("://") != std::string::npos) {
            diags.push_back({Diagnostic::Level::Warning, "remote image '" + src + "' is not supported; download it first", file});
            n.attrs["missing"] = "1";
        } else if (!src.empty() && !fs::is_regular_file(doc_dir / u8path(src), ec)) {
            diags.push_back({Diagnostic::Level::Warning, "image '" + src + "' not found", file});
            n.attrs["missing"] = "1";
        }
        if (n.type == NodeType::Image && !n.attr("missing").empty()) {
            // Inline images have no placeholder: drop to their alt text.
            n.type = NodeType::Emph;
        }
    }
    for (auto& c : n.children) check_images(c, doc_dir, diags, file);
}

// "main.tex:109: LaTeX Error: ..." -> a diagnostic on the Markdown line that
// produced that .tex line.
Diagnostic latex_error_diagnostic(const std::string& message, const latex::LineMap& map, const std::string& md_file,
                                  const std::string& tex_stem) {
    const std::string prefix = tex_stem + ".tex:";
    size_t at = message.find(prefix);
    if (at != std::string::npos) {
        size_t start = at + prefix.size(), end = start;
        while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end]))) ++end;
        if (end > start && end < message.size() && message[end] == ':') {
            size_t tex_line = std::stoul(message.substr(start, end - start));
            std::string rest = std::string(trim(std::string_view(message).substr(end + 1)));
            if (size_t md_line = latex::markdown_line_for(map, tex_line); md_line)
                return {Diagnostic::Level::Error, "LaTeX: " + rest, md_file, md_line};
        }
    }
    return {Diagnostic::Level::Error, "LaTeX: " + message, md_file, 0};
}

bool missing_from_cache(const std::string& output) {
    for (const char* needle : {"only-cached", "Cannot proceed without .vf", "not available in the local cache",
                               "not found", "unable to find"})
        if (output.find(needle) != std::string::npos) return true;
    return false;
}

}  // namespace

TexOutput generate_tex(const fs::path& input, const std::optional<std::string>& style, Diagnostics& diags,
                       const std::string* buffer) {
    TexOutput out;
    const std::string file = path_str(input);
    std::string source = buffer ? *buffer : read_file(input);
    fs::path doc_dir = fs::absolute(input).parent_path();

    // The style decides how the body is parsed (tag delimiters, list labels),
    // so resolve it from the front matter first.
    size_t fm_line = 0;
    std::string fm = extract_front_matter(source, &fm_line);
    Diagnostics quiet;  // front matter problems are reported by the full parse below
    auto meta = fm.empty() ? std::map<std::string, std::string>{} : parse_front_matter(fm, file, fm_line, quiet);
    out.style = resolve_style(meta, doc_dir, style, diags);
    apply_document_lists(out.style, meta, diags, file);

    out.doc = parse_document(source, diags, file, ParseOptions::from_style(out.style));
    apply_rules(out.doc.root, out.style);
    check_images(out.doc.root, doc_dir, diags, file);

    latex::EmitOptions eo;
    if (auto it = out.doc.meta.find("bibliography"); it != out.doc.meta.end() && !it->second.empty()) {
        fs::path bib = u8path(it->second);
        std::error_code ec;
        if (!fs::is_regular_file(doc_dir / bib, ec)) {
            diags.push_back({Diagnostic::Level::Error, "bibliography file '" + it->second + "' not found", file});
        } else {
            bib.replace_extension();
            eo.bibliography = path_str(bib);
        }
    }
    out.tex = latex::emit_document(out.doc, out.style, eo, diags, &out.line_map);
    return out;
}

BuildResult build_pdf(const BuildOptions& opts, Diagnostics& diags) {
    BuildResult r;
    auto gen = generate_tex(opts.input, opts.style, diags, opts.source ? &*opts.source : nullptr);
    for (auto& d : diags)
        if (d.level == Diagnostic::Level::Error) throw PlaciError("the document has errors; fix them and build again");

    fs::path doc_dir = fs::absolute(opts.input).parent_path();
    fs::path build_dir = doc_dir / ".placi";
    fs::create_directories(build_dir);
    r.tex = build_dir / (opts.job_name.empty() ? opts.input.stem() : u8path(opts.job_name));
    r.tex += ".tex";
    write_file(r.tex, gen.tex);

    auto tectonic = find_tectonic();
    if (!tectonic) throw PlaciError("Tectonic (the LaTeX engine) was not found; reinstall PlaciDocs or set PLACI_TECTONIC");

    TectonicOptions to;
    to.tex_file = r.tex;
    to.out_dir = build_dir;
    to.search_paths = {doc_dir};
    if (!gen.style.base_dir.empty()) to.search_paths.push_back(u8path(gen.style.base_dir));
    // The LaTeX packages ship with PlaciDocs (texcache/ next to the executable),
    // so builds never need the network unless the user asks for it.
    to.cache_dir = find_texcache();
    to.only_cached = !opts.online && !to.cache_dir.empty();
    r.compile = run_tectonic(*tectonic, to);
    if (!r.compile.ok && to.only_cached && missing_from_cache(r.compile.output)) {
        // A package or font is not in the bundled cache: fetch it once.
        diags.push_back({Diagnostic::Level::Warning,
                         "some LaTeX files are not in the bundled cache; downloading them (needs internet this once)"});
        to.only_cached = false;
        std::error_code ec;
        auto probe = to.cache_dir / ".placi-write-test";
        std::ofstream(probe).put('x');
        if (!fs::exists(probe, ec)) to.cache_dir.clear();  // read-only install: use the user cache instead
        fs::remove(probe, ec);
        r.compile = run_tectonic(*tectonic, to);
    }
    if (!r.compile.ok) {
        for (auto& e : r.compile.errors)
            diags.push_back(latex_error_diagnostic(e, gen.line_map, path_str(opts.input), path_str(r.tex.stem())));
        return r;
    }

    r.pdf = opts.output.value_or(doc_dir / opts.input.stem().concat(".pdf"));
    if (fs::absolute(r.pdf) != fs::absolute(r.compile.pdf)) {
        if (r.pdf.has_parent_path()) fs::create_directories(r.pdf.parent_path());
        // Remove first: overwrite_existing is unreliable in MinGW's libstdc++.
        std::error_code ec;
        fs::remove(r.pdf, ec);
        fs::copy_file(r.compile.pdf, r.pdf);
    }
    return r;
}

}  // namespace placi
