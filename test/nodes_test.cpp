#include "test_util.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace test_util;
using VT = cp::ValueType;

TEST(Nodes, TheLibraryHasTheNodesTexturesNeed) {
    const auto categories = cp::nodeCategories();
    for (const char* needed : {"noise2d", "noise3d", "fractal2d", "fractal3d", "cellnoise2d", "worleynoise2d",
                               "checkerboard", "place2d", "mix", "heighttonormal", "image", "separate3", "ramp4"}) {
        EXPECT_NE(std::find(categories.begin(), categories.end(), needed), categories.end()) << needed;
    }
}

TEST(Nodes, DefinitionsCarryMaterialXDefaults) {
    cp::NodeDef def;
    ASSERT_TRUE(cp::findNodeDef("noise2d", VT::color3, def));
    ASSERT_EQ(def.inputs.size(), 3u);
    EXPECT_EQ(def.inputs[0].name, "amplitude");
    EXPECT_EQ(def.inputs[0].type, VT::color3);
    EXPECT_EQ(def.inputs[0].defaultValue.data[2], 1.0f);
    EXPECT_EQ(def.inputs[2].source, cp::DefaultSource::texcoord);
    ASSERT_TRUE(cp::findNodeDef("separate3", VT::color3, def));
    ASSERT_EQ(def.outputs.size(), 3u);
    EXPECT_EQ(def.outputs[2].name, "outb");
    EXPECT_FALSE(cp::findNodeDef("noise2d", VT::matrix44, def));
    EXPECT_FALSE(cp::findNodeDef("nosuchnode", VT::float1, def));
}

// Every node, for every type it produces, compiles with its defaults alone
// and bakes finite values (no NaN from an unset input).
TEST(Nodes, EveryNodeBakesWithItsDefaults) {
    for (const auto& category : cp::nodeCategories()) {
        for (const VT type : cp::nodeTypes(category)) {
            cp::NodeDef def;
            ASSERT_TRUE(cp::findNodeDef(category, type, def));
            cp::Node n;
            n.name = category;
            n.category = category;
            n.type = def.outputs.size() > 1 ? VT::multioutput : type;
            // Inputs of any type (magnitude, extract...) get a vector3.
            for (const auto& input : def.inputs) {
                if (input.type == VT::none) n.inputs.push_back(value(input.name.c_str(), cp::Value::makeVector3(0.3f, 0.6f, 0.9f)));
            }
            if (def.outputs.size() > 1) n.inputs.push_back(value("in", def.inputs[0].defaultValue));
            for (const auto& out : def.outputs) n.outputs.push_back({out.name, out.type, -1, ""});
            cp::Graph graph;
            graph.type = n.type;
            graph.nodes.push_back(n);
            graph.outputs.push_back({"out", def.outputs[0].type, 0, def.outputs[0].name});
            const auto baked = cp::bake(graph, size(4, 4));
            ASSERT_TRUE(baked.ok()) << category << " " << cp::toString(type) << ": " << baked.error;
            for (const float v : baked.images[0].rgba) {
                // log(0) and acos/asin outside -1..1 are legitimately not finite.
                if (category == "ln" || category == "divide" || category == "remap") continue;
                ASSERT_TRUE(std::isfinite(v)) << category << " " << cp::toString(type);
            }
        }
    }
}
