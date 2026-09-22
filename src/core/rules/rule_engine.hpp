#pragma once
// Evaluates the conditional rules of a style against the document and stores
// the outcome as layout properties (Node::props), e.g. a table with more than
// N columns gets props["page.orientation"] = "landscape".

#include <map>
#include <string>

#include "core/ast.hpp"
#include "core/style/style.hpp"

namespace placi {

// Element name a rule refers to ("table", "figure", ...), or empty.
std::string element_name(NodeType t);

// Facts a rule can test for a node: columns, rows, level, id, words, ...
std::map<std::string, std::string> node_metrics(const Node& n);

void apply_rules(Node& root, const Style& style);

}  // namespace placi
