#pragma once

#include <campello_procedural/graph.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace systems::leal::campello_procedural {

/// Graphs read from KHR_texture_procedurals JSON, or why reading failed.
struct ReadResult {
    std::vector<Graph> graphs;
    std::string error;  ///< empty on success

    bool ok() const { return error.empty(); }
};

/// Reads procedural graphs from JSON: a whole glTF document (its
/// extensions.KHR_texture_procedurals), the extension object
/// ({"procedurals": [...]}) or a single graph object.
///
/// The draft extension (KhronosGroup/glTF#2381) shows two layouts, and both
/// are accepted: inputs and outputs as objects keyed by name with
/// connections by name (the README's examples), or as arrays of named
/// ports with connections by index (its JSON schema).
ReadResult readProcedurals(std::string_view json);

/// Writes one graph as a KHR_texture_procedurals "nodegraph" object, in the
/// README's layout (ports keyed by name, connections by name). `indent` < 0
/// writes it on one line.
std::string writeGraph(const Graph& graph, int indent = -1);

/// Writes the extension object {"procedurals": [...]}.
std::string writeProcedurals(const std::vector<Graph>& graphs, int indent = -1);

}  // namespace systems::leal::campello_procedural
