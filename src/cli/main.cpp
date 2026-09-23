// placi — PlaciDocs command line.
//
//   placi build  doc.md [-s style] [-o out.pdf] [--online]
//   placi tex    doc.md [-s style] [-o out.tex]
//   placi check-style style.yaml
//   placi ast    doc.md
//   placi styles

#include <CLI11.hpp>

#include <chrono>
#include <iostream>

#include "core/pipeline.hpp"
#include "core/rules/rule_engine.hpp"
#include "core/style/style_loader.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace placi;

namespace {

int report(const Diagnostics& diags) {
    int errors = 0;
    for (auto& d : diags) {
        std::cerr << d.describe() << "\n";
        errors += d.level == Diagnostic::Level::Error;
    }
    return errors;
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

std::optional<std::string> opt(const std::string& s) {
    return s.empty() ? std::nullopt : std::optional<std::string>(s);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    CLI::App app{"PlaciDocs: write Markdown, get a PDF that follows your institution's writing rules."};
    argv = app.ensure_utf8(argv);
    app.require_subcommand(1);
    app.set_version_flag("-V,--version", "placi 0.1.0");

    std::string input, style, output;
    bool online = false, verbose = false;

    auto* build = app.add_subcommand("build", "Build a PDF from a Markdown document");
    build->add_option("input", input, "Markdown file")->required()->check(CLI::ExistingFile);
    build->add_option("-s,--style", style, "Style name or .yaml file (overrides the front matter)");
    build->add_option("-o,--output", output, "Output PDF (default: next to the input)");
    build->add_flag("--online", online, "Let the LaTeX engine download packages missing from the bundled cache");
    build->add_flag("-v,--verbose", verbose, "Print timings and the full LaTeX log on failure");

    auto* tex = app.add_subcommand("tex", "Write the generated LaTeX source");
    tex->add_option("input", input, "Markdown file")->required()->check(CLI::ExistingFile);
    tex->add_option("-s,--style", style, "Style name or .yaml file");
    tex->add_option("-o,--output", output, "Output .tex (default: stdout)");

    auto* check = app.add_subcommand("check-style", "Validate a style file and summarise it");
    check->add_option("style", style, "Style name or .yaml file")->required();

    auto* ast = app.add_subcommand("ast", "Print the parsed document tree (for debugging)");
    ast->add_option("input", input, "Markdown file")->required()->check(CLI::ExistingFile);
    ast->add_option("-s,--style", style, "Style name or .yaml file (overrides the front matter)");

    auto* styles = app.add_subcommand("styles", "List the built-in styles");

    CLI11_PARSE(app, argc, argv);

    Diagnostics diags;
    try {
        if (*build) {
            auto t0 = std::chrono::steady_clock::now();
            BuildOptions bo;
            bo.input = u8path(input);
            bo.style = opt(style);
            if (!output.empty()) bo.output = u8path(output);
            bo.online = online;
            auto r = build_pdf(bo, diags);
            int errors = report(diags);
            if (!r.compile.ok) {
                // The errors were already reported above, on the Markdown lines that caused them.
                std::cerr << "LaTeX failed; generated source: " << path_str(r.tex) << "\n";
                if (verbose) std::cerr << r.compile.output << "\n";
                return 1;
            }
            std::cout << "wrote " << path_str(r.pdf);
            if (verbose) std::cout << " in " << static_cast<long>(ms_since(t0)) << " ms";
            std::cout << "\n";
            return errors ? 1 : 0;
        }
        if (*tex) {
            auto t0 = std::chrono::steady_clock::now();
            auto out = generate_tex(u8path(input), opt(style), diags);
            double ms = ms_since(t0);
            int errors = report(diags);
            if (output.empty()) {
                std::cout << out.tex;
            } else {
                write_file(u8path(output), out.tex);
                std::cerr << "wrote " << output << " (" << ms << " ms)\n";
            }
            return errors ? 1 : 0;
        }
        if (*check) {
            auto dirs = builtin_style_dirs();
            auto path = find_style(style, fs::current_path(), dirs);
            if (!path) throw PlaciError("style '" + style + "' not found");
            Style st = load_style(*path, diags, dirs);
            int errors = report(diags);
            std::cout << "style '" << st.name << "' (" << path_str(*path) << ") is valid\n"
                      << "  page     " << st.page.size << ", margins T" << st.page.margin.top << " B" << st.page.margin.bottom
                      << " L" << st.page.margin.left << " R" << st.page.margin.right << "\n"
                      << "  font     " << (st.font.family.empty() ? "Latin Modern" : st.font.family) << " " << st.font.size << "\n"
                      << "  spacing  " << st.paragraph.line_spacing << ", indent " << st.paragraph.indent << "\n"
                      << "  rules    " << st.rules.size() << "\n";
            for (auto& r : st.rules) {
                std::cout << "    - " << r.element << " (" << r.source << "):";
                for (auto& [k, v] : r.set) std::cout << " " << k << "=" << v;
                std::cout << "\n";
            }
            return errors ? 1 : 0;
        }
        if (*ast) {
            auto out = generate_tex(u8path(input), opt(style), diags);
            report(diags);
            for (auto& [k, v] : out.doc.meta) std::cout << "meta " << k << " = " << v << "\n";
            std::cout << dump(out.doc.root);
            return 0;
        }
        if (*styles) {
            for (auto& dir : builtin_style_dirs())
                for (auto& e : fs::directory_iterator(dir))
                    if (e.path().extension() == ".yaml") {
                        Diagnostics quiet;
                        std::string name = path_str(e.path().stem());
                        try {
                            auto st = load_style(e.path(), quiet, builtin_style_dirs());
                            std::cout << name << "  —  " << st.name << "\n";
                        } catch (const PlaciError& err) {
                            std::cout << name << "  (invalid: " << err.what() << ")\n";
                        }
                    }
            return 0;
        }
    } catch (const PlaciError& e) {
        report(diags);
        std::cerr << "error: " << e.describe() << "\n";
        return 1;
    } catch (const std::exception& e) {
        report(diags);
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
