#include "test_util.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace test_util;
using VT = cp::ValueType;

namespace {

// texcoord -> + shift -> place2d (scale, rotate) -> <lattice node>.
cp::Graph lattice(cp::Node latticeNode, float shiftU, float scale, float rotate = 0.0f) {
    cp::Graph graph = single({
        node("texcoord", VT::vector2),
        node("add", VT::vector2, {fromNode("in1", VT::vector2, 0), value("in2", cp::Value::makeVector2(shiftU, 0.0f))}),
        node("multiply", VT::vector2, {fromNode("in1", VT::vector2, 1), value("in2", cp::Value::makeVector2(scale, scale))}),
        node("rotate2d", VT::vector2, {fromNode("in", VT::vector2, 2), value("amount", cp::Value::makeFloat(rotate))}),
    });
    latticeNode.inputs.push_back(fromNode("texcoord", VT::vector2, 3));
    graph.nodes.push_back(latticeNode);
    graph.type = latticeNode.type;
    graph.outputs = {output("out", latticeNode.type, int(graph.nodes.size()) - 1)};
    return graph;
}

cp::BakeResult bakeTileable(const cp::Graph& graph, bool tileable = true) {
    auto options = size(32, 32);
    options.tileable = tileable;
    return cp::bake(graph, options);
}

float maxDifference(const cp::Image& a, const cp::Image& b) {
    float worst = 0.0f;
    for (size_t i = 0; i < a.rgba.size(); ++i) worst = std::max(worst, std::fabs(a.rgba[i] - b.rgba[i]));
    return worst;
}

}  // namespace

// The image repeats: the graph moved by one whole tile bakes the same image.
TEST(Tileable, ShiftingByATileChangesNothing) {
    for (const char* category : {"noise2d", "fractal2d", "cellnoise2d", "worleynoise2d"}) {
        const auto make = [&](float shift) {
            cp::Node n = node(category, VT::float1);
            if (std::string(category) == "fractal2d") n.inputs.push_back(value("octaves", cp::Value::makeInteger(4)));
            return lattice(n, shift, 4.0f);
        };
        const auto a = bakeTileable(make(0.0f));
        const auto b = bakeTileable(make(1.0f));
        ASSERT_TRUE(a.ok() && b.ok()) << category << a.error << b.error;
        EXPECT_TRUE(a.warnings.empty()) << category << ": " << a.warnings.front();
        EXPECT_LT(maxDifference(a.images[0], b.images[0]), 1e-4f) << category;
        // Without tiling, the noise beyond the tile is different noise.
        const auto c = bakeTileable(make(0.0f), false);
        const auto d = bakeTileable(make(1.0f), false);
        ASSERT_TRUE(c.ok() && d.ok());
        EXPECT_GT(maxDifference(c.images[0], d.images[0]), 0.05f) << category;
    }
}

// Per-cell values over floored coordinates (a tone per brick): 2 bricks by
// 8 courses per tile wrap at (2, 8).
TEST(Tileable, FlooredCellsTile) {
    const auto make = [](float shift) {
        cp::Graph graph = single({
            node("texcoord", VT::vector2),
            node("add", VT::vector2, {fromNode("in1", VT::vector2, 0), value("in2", cp::Value::makeVector2(shift, 0.0f))}),
            node("multiply", VT::vector2, {fromNode("in1", VT::vector2, 1), value("in2", cp::Value::makeVector2(2, 8))}),
            node("floor", VT::vector2, {fromNode("in", VT::vector2, 2)}),
            node("cellnoise2d", VT::float1, {fromNode("texcoord", VT::vector2, 3)}),
        });
        return graph;
    };
    const auto a = bakeTileable(make(0.0f));
    const auto b = bakeTileable(make(1.0f));
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_TRUE(a.warnings.empty());
    EXPECT_EQ(a.images[0].rgba, b.images[0].rgba);
}

// A quarter turn still repeats; 30 degrees or a fractional number of cells
// per tile doesn't, and says so.
TEST(Tileable, WarnsWhenCoordinatesDontRepeat) {
    const auto quarter = bakeTileable(lattice(node("noise2d", VT::float1), 0.0f, 4.0f, 90.0f));
    ASSERT_TRUE(quarter.ok());
    EXPECT_TRUE(quarter.warnings.empty()) << quarter.warnings.front();
    const auto quarterShifted = bakeTileable(lattice(node("noise2d", VT::float1), 1.0f, 4.0f, 90.0f));
    EXPECT_LT(maxDifference(quarter.images[0], quarterShifted.images[0]), 1e-4f);

    const auto turned = bakeTileable(lattice(node("noise2d", VT::float1), 0.0f, 4.0f, 30.0f));
    ASSERT_TRUE(turned.ok()) << "a warning, not an error";
    ASSERT_EQ(turned.warnings.size(), 1u);
    EXPECT_NE(turned.warnings[0].find("noise2d"), std::string::npos) << turned.warnings[0];

    const auto fractional = bakeTileable(lattice(node("noise2d", VT::float1), 0.0f, 4.5f));
    EXPECT_EQ(fractional.warnings.size(), 1u);

    cp::Node fractal = node("fractal2d", VT::float1);
    fractal.inputs.push_back(value("lacunarity", cp::Value::makeFloat(2.5f)));
    const auto lacunarity = bakeTileable(lattice(fractal, 0.0f, 4.0f));
    ASSERT_EQ(lacunarity.warnings.size(), 1u);
    EXPECT_NE(lacunarity.warnings[0].find("lacunarity"), std::string::npos);
}

// The seam: the last column continues into the first as smoothly as any
// two neighbouring columns.
TEST(Tileable, NoSeamAtTheEdges) {
    cp::Node n = node("fractal2d", VT::float1);
    const auto baked = bakeTileable(lattice(n, 0.0f, 3.0f));
    ASSERT_TRUE(baked.ok());
    const auto& image = baked.images[0];
    float seam = 0.0f, inside = 0.0f;
    for (uint32_t y = 0; y < image.height; ++y) {
        seam = std::max(seam, std::fabs(image.texel(0, y)[0] - image.texel(image.width - 1, y)[0]));
        for (uint32_t x = 1; x < image.width; ++x) {
            inside = std::max(inside, std::fabs(image.texel(x, y)[0] - image.texel(x - 1, y)[0]));
        }
    }
    EXPECT_LE(seam, inside * 1.01f);
}
