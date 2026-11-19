#include "test_util.hpp"

#include <gtest/gtest.h>

using namespace test_util;
using VT = cp::ValueType;

TEST(Version, MatchesTheProject) { EXPECT_EQ(cp::getVersion(), "0.2.0"); }

// The draft's own example (a whole glTF document, README layout).
TEST(Json, ReadsTheDraftsCheckerboardExample) {
    const auto text = readFile("checkerboard_example.json");
    ASSERT_FALSE(text.empty());
    const auto result = cp::readProcedurals(text);
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.graphs.size(), 1u);
    const auto& graph = result.graphs[0];
    EXPECT_EQ(graph.name, "NG_main");
    EXPECT_EQ(graph.type, VT::color3);
    ASSERT_EQ(graph.inputs.size(), 3u);
    EXPECT_EQ(graph.inputs[0].name, "uvtiling");  // document order is kept
    EXPECT_EQ(graph.inputs[0].value->data[0], 8.0f);
    ASSERT_EQ(graph.nodes.size(), 7u);
    const auto* mix = &graph.nodes[0];
    EXPECT_EQ(mix->category, "mix");
    ASSERT_NE(mix->findInput("fg"), nullptr);
    EXPECT_EQ(mix->findInput("fg")->graphInput, "color1");
    EXPECT_EQ(mix->findInput("mix")->node, 5);
    ASSERT_EQ(graph.outputs.size(), 1u);
    EXPECT_EQ(graph.outputs[0].name, "output_N_mtlxmix_out");
    EXPECT_EQ(graph.outputs[0].node, 0);
}

// The schema's layout: ports in arrays, connections by index.
TEST(Json, ReadsTheSchemaLayout) {
    const char* text = R"({
      "procedurals": [{
        "name": "g", "nodetype": "nodegraph", "type": "multioutput",
        "inputs": [{"name": "scale", "nodetype": "input", "type": "float", "value": 2.0}],
        "nodes": [
          {"name": "sep", "nodetype": "separate2", "type": "multioutput",
           "inputs": [{"name": "in", "nodetype": "input", "type": "vector2", "value": [0.25, 0.75]}],
           "outputs": [{"name": "outx", "nodetype": "output", "type": "float"},
                       {"name": "outy", "nodetype": "output", "type": "float"}]},
          {"name": "mul", "nodetype": "multiply", "type": "float",
           "inputs": [{"name": "in1", "nodetype": "input", "type": "float", "node": 0, "output": 1},
                      {"name": "in2", "nodetype": "input", "type": "float", "input": 0}],
           "outputs": [{"name": "out", "nodetype": "output", "type": "float"}]}
        ],
        "outputs": [{"name": "result", "nodetype": "output", "type": "float", "node": 1}]
      }]
    })";
    const auto result = cp::readProcedurals(text);
    ASSERT_TRUE(result.ok()) << result.error;
    const auto& graph = result.graphs.at(0);
    const auto* in1 = graph.nodes.at(1).findInput("in1");
    ASSERT_NE(in1, nullptr);
    EXPECT_EQ(in1->output, "outy") << "an output index becomes the output's name";
    EXPECT_EQ(graph.nodes.at(1).findInput("in2")->graphInput, "scale") << "an input index becomes its name";
    const auto baked = cp::bake(graph, size(2, 2));
    ASSERT_TRUE(baked.ok()) << baked.error;
    EXPECT_FLOAT_EQ(baked.images[0].rgba[0], 1.5f);  // 0.75 * 2
}

TEST(Json, RoundTripsAGraph) {
    const auto first = cp::readProcedurals(readFile("checkerboard_example.json"));
    ASSERT_TRUE(first.ok()) << first.error;
    const std::string written = cp::writeProcedurals(first.graphs, 2);
    const auto second = cp::readProcedurals(written);
    ASSERT_TRUE(second.ok()) << second.error;
    ASSERT_EQ(second.graphs.size(), 1u);
    EXPECT_EQ(cp::writeGraph(second.graphs[0]), cp::writeGraph(first.graphs[0]));
    // And they bake the same.
    const auto a = cp::bake(first.graphs[0], size(16, 16));
    const auto b = cp::bake(second.graphs[0], size(16, 16));
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_EQ(a.images[0].rgba, b.images[0].rgba);
}

TEST(Json, ReadsASingleGraphObject) {
    const char* text = R"({"name": "one", "nodetype": "nodegraph", "type": "float",
      "nodes": [{"nodetype": "constant", "type": "float",
                 "inputs": {"value": {"nodetype": "input", "type": "float", "value": 0.5}},
                 "outputs": {"out": {"nodetype": "output", "type": "float"}}}],
      "outputs": {"out": {"nodetype": "output", "type": "float", "node": 0}}})";
    const auto result = cp::readProcedurals(text);
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.graphs.size(), 1u);
    EXPECT_EQ(result.graphs[0].name, "one");
}

TEST(Json, KeepsUiDescriptionsAndPositions) {
    cp::Graph graph = single({node("constant", VT::float1, {fromGraphInput("value", VT::float1, "brickWidth")})});
    cp::Input parameter = value("brickWidth", cp::Value::makeFloat(0.24f));
    parameter.uiName = "Brick width";
    parameter.uiFolder = "Bricks";
    parameter.uiMin = 0.05f;
    parameter.uiSoftMax = 0.5f;
    graph.inputs.push_back(parameter);
    graph.nodes[0].position = std::array<float, 2>{10.0f, -4.0f};
    const auto result = cp::readProcedurals(cp::writeGraph(graph));
    ASSERT_TRUE(result.ok()) << result.error;
    const auto& read = result.graphs.at(0);
    EXPECT_EQ(read.inputs.at(0).uiName, "Brick width");
    EXPECT_EQ(read.inputs.at(0).uiFolder, "Bricks");
    EXPECT_EQ(read.inputs.at(0).uiMin, 0.05f);
    EXPECT_EQ(read.inputs.at(0).uiSoftMax, 0.5f);
    EXPECT_FALSE(read.inputs.at(0).uiMax.has_value());
    ASSERT_TRUE(read.nodes.at(0).position.has_value());
    EXPECT_EQ((*read.nodes.at(0).position)[1], -4.0f);
}

TEST(Json, ReportsErrors) {
    EXPECT_FALSE(cp::readProcedurals("{not json").ok());
    EXPECT_FALSE(cp::readProcedurals("[]").ok());
    EXPECT_FALSE(cp::readProcedurals(R"({"asset": {"version": "2.0"}})").ok()) << "no graphs";
    const auto badType = cp::readProcedurals(
        R"({"procedurals": [{"nodetype": "nodegraph", "type": "colour3", "nodes": [], "outputs": {}}]})");
    EXPECT_FALSE(badType.ok());
    EXPECT_NE(badType.error.find("colour3"), std::string::npos) << badType.error;
}
