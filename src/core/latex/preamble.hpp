#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/style/style.hpp"

namespace placi::latex {

// How one list of the style maps onto LaTeX.
struct ListEnv {
    const ListStyle* list = nullptr;
    std::string env;      // figure | table | equation | placifloata ...
    std::string counter;  // counter holding the item number
    std::string ext;      // aux file extension of the list: lof | lot | loe | lofa ...
};

// Everything the preamble must know about the body.
struct DocPlan {
    std::vector<ListEnv> lists;
    std::vector<PageNumberStyle> page_styles;  // defined as placipn<i> / placipn<i>first
    std::string bib_style;
    std::string citation;                      // author-year | numeric

    const ListEnv* env_for_list(const ListStyle* l) const;
    size_t page_style_index(const PageNumberStyle& p);  // registers if new
};

DocPlan make_plan(const Style& style);

// \documentclass ... up to (not including) \begin{document}.
std::string build_preamble(const Style& style, const std::map<std::string, std::string>& meta, const DocPlan& plan);

// Commands issued right after \begin{document}.
std::string build_document_start(const Style& style);

// Heading format commands for one level (used in the preamble and when a tag changes it).
std::string heading_setup(int level, const HeadingStyle& h, bool chapter_number_follows_label);

// Switches page numbering. The page counter restarts only when the kind of
// number changes (e.g. roman -> arabic), not when only the position does.
std::string page_numbering_switch(size_t style_index, const PageNumberStyle& p, const PageNumberStyle* previous);

// Fills {{key}} placeholders of a LaTeX template from the front matter.
// Keys that the front matter does not provide are added to `missing`.
std::string fill_template(const std::string& tmpl, const std::map<std::string, std::string>& meta,
                          const std::string& style_dir, std::vector<std::string>* missing = nullptr);

}  // namespace placi::latex
