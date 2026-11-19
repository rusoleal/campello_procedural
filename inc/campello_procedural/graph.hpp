#pragma once

#include <campello_procedural/value.hpp>

#include <optional>
#include <string>
#include <vector>

namespace systems::leal::campello_procedural {

/// An input port: on a node (its value or what feeds it) or on a graph's
/// interface (a parameter of the graph, with an optional UI description).
/// At most one source is set; none means the node definition's default.
struct Input {
    std::string name;
    ValueType type = ValueType::none;

    std::optional<Value> value;  ///< a constant
    int node = -1;               ///< the index, in Graph::nodes, of the node that feeds it
    std::string output;          ///< that node's output (empty: its first output)
    std::string graphInput;      ///< a graph interface input that feeds it (by name)
    int texture = -1;            ///< a glTF texture (index in the asset's `textures`)

    // UI description (graph interface inputs).
    std::string uiName;
    std::string uiFolder;
    std::string doc;
    std::optional<float> uiMin, uiMax, uiSoftMin, uiSoftMax, uiStep;

    bool isConnected() const { return node >= 0 || !graphInput.empty() || texture >= 0; }
};

/// An output port. On a node: its name and type. On a graph: also the node
/// (and that node's output) it exposes.
struct Output {
    std::string name;
    ValueType type = ValueType::none;
    int node = -1;
    std::string output;  ///< the node's output (empty: its first output)
};

/// A node: a MaterialX standard library category ("noise2d", "mix", ...)
/// producing `type` (or several outputs, ValueType::multioutput).
struct Node {
    std::string name;
    std::string category;  ///< the JSON's "nodetype"
    ValueType type = ValueType::none;
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    std::optional<std::array<float, 2>> position;  ///< in a node editor (xpos, ypos)

    const Input* findInput(std::string_view inputName) const;
    Input* findInput(std::string_view inputName);
};

/// A procedural graph (a KHR_texture_procedurals "nodegraph"): interface
/// inputs, the nodes, and the outputs bound to material texture slots.
struct Graph {
    std::string name;
    ValueType type = ValueType::none;  ///< the output's type, or multioutput
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    std::vector<Node> nodes;

    const Input* findInput(std::string_view inputName) const;
    Input* findInput(std::string_view inputName);
    /// The index of an output in `outputs`, or -1.
    int findOutput(std::string_view outputName) const;
};

}  // namespace systems::leal::campello_procedural
