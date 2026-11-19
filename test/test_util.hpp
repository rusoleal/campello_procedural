#pragma once

#include <campello_procedural/campello_procedural.hpp>

#include <fstream>
#include <sstream>
#include <string>

namespace test_util {

namespace cp = systems::leal::campello_procedural;

inline std::string readFile(const std::string& name) {
    std::ifstream file(std::string(CAMPELLO_PROCEDURAL_TEST_DATA) + "/" + name, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// Graph building helpers.
inline cp::Input value(const char* name, cp::Value v) {
    cp::Input input;
    input.name = name;
    input.type = v.type;
    input.value = std::move(v);
    return input;
}

inline cp::Input fromNode(const char* name, cp::ValueType type, int node, const char* output = "") {
    cp::Input input;
    input.name = name;
    input.type = type;
    input.node = node;
    input.output = output;
    return input;
}

inline cp::Input fromGraphInput(const char* name, cp::ValueType type, const char* graphInput) {
    cp::Input input;
    input.name = name;
    input.type = type;
    input.graphInput = graphInput;
    return input;
}

inline cp::Node node(const char* category, cp::ValueType type, std::vector<cp::Input> inputs = {}) {
    cp::Node n;
    n.name = category;
    n.category = category;
    n.type = type;
    n.inputs = std::move(inputs);
    n.outputs.push_back({"out", type, -1, ""});
    return n;
}

inline cp::Output output(const char* name, cp::ValueType type, int node, const char* nodeOutput = "") {
    return cp::Output{name, type, node, nodeOutput};
}

// A graph with one output fed by its last node.
inline cp::Graph single(std::vector<cp::Node> nodes) {
    cp::Graph graph;
    graph.name = "test";
    graph.type = nodes.back().type;
    graph.nodes = std::move(nodes);
    graph.outputs.push_back(output("out", graph.type, int(graph.nodes.size()) - 1));
    return graph;
}

inline cp::BakeOptions size(uint32_t w, uint32_t h) {
    cp::BakeOptions options;
    options.width = w;
    options.height = h;
    return options;
}

}  // namespace test_util
