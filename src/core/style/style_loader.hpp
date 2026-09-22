#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/style/style.hpp"
#include "core/util.hpp"

namespace placi {

// Loads a YAML style file. `extends: other` is resolved relative to the file
// first, then in `search_dirs` (built-in styles). Unknown keys produce
// warnings; invalid values throw PlaciError with file and line.
Style load_style(const fs::path& file, Diagnostics& diags, const std::vector<fs::path>& search_dirs = {});
Style load_style_string(std::string_view yaml, const std::string& filename, Diagnostics& diags,
                        const std::vector<fs::path>& search_dirs = {});

// Finds `name` (with or without .yaml) relative to `base_dir`, then in `search_dirs`.
std::optional<fs::path> find_style(const std::string& name, const fs::path& base_dir,
                                   const std::vector<fs::path>& search_dirs);

// Flattens front matter into key -> string. Lists are joined with ", ",
// nested maps become "parent.child".
std::map<std::string, std::string> parse_front_matter(const std::string& yaml, const std::string& filename,
                                                      size_t first_line, Diagnostics& diags);

// Applies document-level lists from the (flattened) front matter:
//   lists.grafik.prefix: Grafik   lists.grafik.label: gfk   ...
// These are what the GUI's "Tambah Daftar" writes.
void apply_document_lists(Style& style, const std::map<std::string, std::string>& meta, Diagnostics& diags,
                          const std::string& filename = {});

}  // namespace placi
