#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/util.hpp"

namespace placi {

struct TectonicOptions {
    fs::path tex_file;
    fs::path out_dir;
    std::vector<fs::path> search_paths;  // where images / .bib files live
    bool keep_logs = true;
    fs::path cache_dir;                  // bundled package cache (TECTONIC_CACHE_DIR); empty = user cache
    bool only_cached = false;            // offline mode: never download packages
};

struct CompileResult {
    bool ok = false;
    fs::path pdf;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::string output;
};

// Looks for the bundled Tectonic next to the executable, then $PLACI_TECTONIC, then PATH.
std::optional<fs::path> find_tectonic();

// The package cache that ships with PlaciDocs: texcache/ next to the executable.
fs::path find_texcache();

CompileResult run_tectonic(const fs::path& tectonic, const TectonicOptions& opts);

}  // namespace placi
