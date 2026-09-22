#include "core/compile/tectonic.hpp"

#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/compile/process.hpp"

namespace placi {

namespace {
#ifdef _WIN32
constexpr const char* kExe = "tectonic.exe";
constexpr char kPathSep = ';';
#else
constexpr const char* kExe = "tectonic";
constexpr char kPathSep = ':';
#endif

bool exists_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
}  // namespace

std::optional<fs::path> find_tectonic() {
    auto dir = executable_dir();
    for (auto& p : {dir / kExe, dir / "tectonic" / kExe, dir.parent_path() / "third_party" / "tectonic" / kExe})
        if (exists_file(p)) return p;
    if (const char* env = std::getenv("PLACI_TECTONIC"); env && exists_file(u8path(env))) return u8path(env);
    if (const char* path = std::getenv("PATH")) {
        std::string_view all(path);
        size_t start = 0;
        while (start <= all.size()) {
            size_t sep = all.find(kPathSep, start);
            auto entry = all.substr(start, sep == std::string_view::npos ? std::string_view::npos : sep - start);
            if (!entry.empty() && exists_file(u8path(entry) / kExe)) return u8path(entry) / kExe;
            if (sep == std::string_view::npos) break;
            start = sep + 1;
        }
    }
    return std::nullopt;
}

fs::path find_texcache() {
    if (const char* env = std::getenv("PLACI_TEXCACHE"); env && *env) return u8path(env);
    std::error_code ec;
    auto p = executable_dir() / "texcache";
    return fs::is_directory(p, ec) ? p : fs::path();
}

namespace {
void set_env(const char* name, const fs::path& value) {
#ifdef _WIN32
    std::wstring wname(name, name + std::strlen(name));
    SetEnvironmentVariableW(wname.c_str(), value.wstring().c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}
}  // namespace

CompileResult run_tectonic(const fs::path& tectonic, const TectonicOptions& opts) {
    // Child processes inherit our environment.
    if (!opts.cache_dir.empty()) set_env("TECTONIC_CACHE_DIR", opts.cache_dir);
    std::vector<std::string> args{"--chatter", "minimal", "--color", "never", "--outdir", path_str(opts.out_dir)};
    if (opts.keep_logs) args.push_back("--keep-logs");
    if (opts.only_cached) args.push_back("--only-cached");
    for (auto& sp : opts.search_paths) args.push_back("-Zsearch-path=" + path_str(sp));
    args.push_back(path_str(opts.tex_file));

    CompileResult r;
    auto proc = run_process(tectonic, args, opts.out_dir);
    r.output = std::move(proc.output);
    for (auto line : split_lines(r.output)) {
        if (line.rfind("error:", 0) == 0) r.errors.emplace_back(trim(line.substr(6)));
        else if (line.rfind("warning:", 0) == 0) r.warnings.emplace_back(trim(line.substr(8)));
    }
    r.pdf = opts.out_dir / opts.tex_file.stem();
    r.pdf += ".pdf";
    r.ok = proc.exit_code == 0 && exists_file(r.pdf);
    if (!r.ok && r.errors.empty()) r.errors.push_back("tectonic exited with code " + std::to_string(proc.exit_code));
    return r;
}

}  // namespace placi
