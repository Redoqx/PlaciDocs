#pragma once

#include <string>
#include <string_view>

namespace placi::latex {

// Escapes text for use in a LaTeX paragraph.
std::string escape(std::string_view s);

// Escapes a URL for \href / \url.
std::string escape_url(std::string_view s);

// Turns a numbering template into a LaTeX expression for `counter`.
//   "{h1}.{n}" with counter "section"  ->  "\arabic{chapter}.\arabic{section}"
// Tokens: {n} {roman} {Roman} {alpha} {Alpha} {h1}..{h6}; other text is literal.
std::string numbering_expr(std::string_view tmpl, std::string_view counter);

// Replaces "{num}" in a label template with `number`, escaping the rest.
std::string label_expr(std::string_view tmpl, std::string_view number);

// "\fontsize{14pt}{16.8pt}\selectfont" (or "" for an empty size).
std::string fontsize_cmd(std::string_view size);

// Heading level (1..6) -> LaTeX sectioning command / counter name.
const char* section_command(int level);

}  // namespace placi::latex
