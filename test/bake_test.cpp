#include "test_util.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace test_util;
using VT = cp::ValueType;

namespace {

const float* at(const cp::Image& image, uint32_t x, uint32_t y) { return image.texel(x, y); }

// A graph whose output is a texcoord channel (0: u, 1: v).
cp::Graph texcoordChannel(int channel) {
    return single({node("texcoord", VT::vector2),
                   node("extract", VT::float1,
                        {fromNode("in", VT::vector2, 0), value("index", cp::Value::makeInteger(channel))})});
}

}  // namespace

// The draft's checkerboard: 8 x 8 squares, colour1 where floor(u*8) +
// floor(v*8) is odd. Texel (0, 0) is the top-left: u ~ 0, v ~ 1.
TEST(Bake, TheDraftsCheckerboard) {
    const auto read = cp::readProcedurals(readFile("checkerboard_example.json"));
    ASSERT_TRUE(read.ok()) << read.error;
    const auto baked = cp::bake(read.graphs[0], size(64, 64));
    ASSERT_TRUE(baked.ok()) << baked.error;
    const auto& image = baked.images[0];
    const float color1[3] = {1.0f, 0.094118f, 0.031373f};
    const float color2[3] = {0.035294f, 0.090196f, 0.878431f};
    // (0, 0): floor(0) + floor(7.9) = 7, odd: colour1. One square right: colour2.
    for (int c = 0; c < 3; ++c) {
        EXPECT_NEAR(at(image, 0, 0)[c], color1[c], 1e-5f);
        EXPECT_NEAR(at(image, 8, 0)[c], color2[c], 1e-5f);
        EXPECT_NEAR(at(image, 8, 8)[c], color1[c], 1e-5f);
    }
    EXPECT_EQ(at(image, 0, 0)[3], 1.0f);
}

TEST(Bake, TexelCentresAreMaterialXCoordinates) {
    const auto u = cp::bake(texcoordChannel(0), size(4, 2));
    const auto v = cp::bake(texcoordChannel(1), size(4, 2));
    ASSERT_TRUE(u.ok() && v.ok()) << u.error << v.error;
    EXPECT_FLOAT_EQ(at(u.images[0], 0, 0)[0], 0.125f);
    EXPECT_FLOAT_EQ(at(u.images[0], 3, 0)[0], 0.875f);
    EXPECT_FLOAT_EQ(at(v.images[0], 0, 0)[0], 0.75f) << "row 0 is the top: v up";
    EXPECT_FLOAT_EQ(at(v.images[0], 0, 1)[0], 0.25f);
}

// One graph, several outputs (a material's slots), baked in one pass, and
// its interface inputs overridden per bake.
TEST(Bake, AllOutputsAndInputOverrides) {
    cp::Graph graph;
    graph.type = VT::multioutput;
    graph.inputs.push_back(value("tint", cp::Value::makeColor3(1.0f, 0.0f, 0.0f)));
    graph.inputs.push_back(value("rough", cp::Value::makeFloat(0.25f)));
    graph.nodes = {
        node("ramplr", VT::float1, {value("valuel", cp::Value::makeFloat(0.0f)), value("valuer", cp::Value::makeFloat(1.0f))}),
        node("multiply", VT::color3, {fromGraphInput("in1", VT::color3, "tint"), fromNode("in2", VT::float1, 0)}),
        node("combine3", VT::vector3, {value("in1", cp::Value::makeFloat(0.0f)), fromGraphInput("in2", VT::float1, "rough"),
                                       value("in3", cp::Value::makeFloat(0.0f))}),
    };
    graph.outputs = {output("base_color", VT::color3, 1), output("metallic_roughness", VT::vector3, 2),
                     output("height", VT::float1, 0)};
    auto options = size(8, 2);
    auto baked = cp::bake(graph, options);
    ASSERT_TRUE(baked.ok()) << baked.error;
    ASSERT_EQ(baked.names.size(), 3u);
    ASSERT_NE(baked.find("metallic_roughness"), nullptr);
    EXPECT_FLOAT_EQ(at(*baked.find("metallic_roughness"), 3, 1)[1], 0.25f);
    EXPECT_NEAR(at(*baked.find("base_color"), 7, 0)[0], 0.9375f, 1e-6f);
    EXPECT_EQ(at(*baked.find("base_color"), 7, 0)[1], 0.0f);

    options.inputs["tint"] = cp::Value::makeColor3(0.0f, 1.0f, 0.0f);
    options.inputs["rough"] = cp::Value::makeFloat(0.8f);
    options.outputs = {"base_color", "metallic_roughness"};
    baked = cp::bake(graph, options);
    ASSERT_TRUE(baked.ok()) << baked.error;
    ASSERT_EQ(baked.names.size(), 2u) << "only the outputs asked for";
    EXPECT_EQ(at(*baked.find("base_color"), 7, 0)[0], 0.0f);
    EXPECT_NEAR(at(*baked.find("base_color"), 7, 0)[1], 0.9375f, 1e-6f);
    EXPECT_FLOAT_EQ(at(*baked.find("metallic_roughness"), 0, 0)[1], 0.8f);

    options.inputs["nosuch"] = cp::Value::makeFloat(1.0f);
    EXPECT_FALSE(cp::bake(graph, options).ok()) << "an unknown input is an error";
}

// heighttonormal: a height rising with u tilts the normal towards -x.
TEST(Bake, HeightToNormalFollowsTheSlope) {
    for (int channel = 0; channel < 2; ++channel) {
        cp::Graph graph = texcoordChannel(channel);
        graph.nodes.push_back(node("heighttonormal", VT::vector3,
                                   {fromNode("in", VT::float1, 1), value("scale", cp::Value::makeFloat(1.0f))}));
        graph.type = VT::vector3;
        graph.outputs = {output("normal", VT::vector3, 2)};
        const auto baked = cp::bake(graph, size(16, 16));
        ASSERT_TRUE(baked.ok()) << baked.error;
        const float* n = at(baked.images[0], 8, 8);
        const float s = 0.5f - 0.5f / std::sqrt(2.0f);  // -1/sqrt(2), encoded
        EXPECT_NEAR(n[channel], s, 1e-4f) << "slope 1 along channel " << channel;
        EXPECT_NEAR(n[1 - channel], 0.5f, 1e-4f);
        EXPECT_NEAR(n[2], 0.5f + 0.5f / std::sqrt(2.0f), 1e-4f);
    }
}

// A normal from a height also used by another output: the other output
// still sees the centre's values (the height is evaluated at neighbours).
TEST(Bake, HeightToNormalLeavesSharedNodesAtTheCentre) {
    cp::Graph graph = texcoordChannel(0);
    graph.nodes.push_back(node("heighttonormal", VT::vector3, {fromNode("in", VT::float1, 1)}));
    graph.type = VT::multioutput;
    graph.outputs = {output("normal", VT::vector3, 2), output("height", VT::float1, 1)};
    const auto baked = cp::bake(graph, size(4, 4));
    ASSERT_TRUE(baked.ok()) << baked.error;
    EXPECT_FLOAT_EQ(at(*baked.find("height"), 1, 0)[0], 0.375f);
}

TEST(Bake, PerlinNoiseIsZeroOnTheLatticeAndBounded) {
    // Scaled so texel centres hit integer coordinates: u * 8 at u = (x + 0.5) / 8 is x + 0.5; shift by 0.5.
    const auto graph = single({
        node("texcoord", VT::vector2),
        node("multiply", VT::vector2, {fromNode("in1", VT::vector2, 0), value("in2", cp::Value::makeVector2(8.0f, 8.0f))}),
        node("subtract", VT::vector2, {fromNode("in1", VT::vector2, 1), value("in2", cp::Value::makeVector2(0.5f, 0.5f))}),
        node("noise2d", VT::float1, {fromNode("texcoord", VT::vector2, 2)}),
    });
    const auto lattice = cp::bake(graph, size(8, 8));
    ASSERT_TRUE(lattice.ok()) << lattice.error;
    for (size_t i = 0; i < lattice.images[0].rgba.size(); i += 4) EXPECT_NEAR(lattice.images[0].rgba[i], 0.0f, 1e-6f);
    const auto fine = cp::bake(graph, size(64, 64));
    ASSERT_TRUE(fine.ok());
    float lo = 1.0f, hi = -1.0f;
    for (size_t i = 0; i < fine.images[0].rgba.size(); i += 4) {
        lo = std::min(lo, fine.images[0].rgba[i]);
        hi = std::max(hi, fine.images[0].rgba[i]);
    }
    EXPECT_GE(lo, -1.0f);
    EXPECT_LE(hi, 1.0f);
    EXPECT_LT(lo, -0.1f) << "noise varies";
    EXPECT_GT(hi, 0.1f);
}

TEST(Bake, FractalWithOneOctaveIsNoise) {
    const auto scaled = [](const char* category, std::vector<cp::Input> extra) {
        std::vector<cp::Input> inputs = std::move(extra);
        inputs.push_back(fromNode("position", VT::vector3, 0));
        return single({node("position", VT::vector3), node(category, VT::float1, std::move(inputs))});
    };
    const auto fractal = cp::bake(scaled("fractal3d", {value("octaves", cp::Value::makeInteger(1))}), size(16, 16));
    const auto noise = cp::bake(scaled("noise3d", {}), size(16, 16));
    ASSERT_TRUE(fractal.ok() && noise.ok()) << fractal.error << noise.error;
    EXPECT_EQ(fractal.images[0].rgba, noise.images[0].rgba);
}

TEST(Bake, CellNoiseIsConstantPerCell) {
    const auto graph = single({
        node("texcoord", VT::vector2),
        node("multiply", VT::vector2, {fromNode("in1", VT::vector2, 0), value("in2", cp::Value::makeVector2(2.0f, 2.0f))}),
        node("cellnoise2d", VT::float1, {fromNode("texcoord", VT::vector2, 1)}),
    });
    const auto baked = cp::bake(graph, size(8, 8));
    ASSERT_TRUE(baked.ok()) << baked.error;
    const auto& image = baked.images[0];
    EXPECT_EQ(at(image, 0, 0)[0], at(image, 3, 3)[0]) << "same cell";
    EXPECT_NE(at(image, 0, 0)[0], at(image, 4, 0)[0]) << "next cell";
    for (size_t i = 0; i < image.rgba.size(); i += 4) {
        EXPECT_GE(image.rgba[i], 0.0f);
        EXPECT_LE(image.rgba[i], 1.0f);
    }
}

TEST(Bake, WorleyWithoutJitterMeasuresToCellCentres) {
    const auto graph = single({
        node("texcoord", VT::vector2),
        node("multiply", VT::vector2, {fromNode("in1", VT::vector2, 0), value("in2", cp::Value::makeVector2(2.0f, 2.0f))}),
        node("worleynoise2d", VT::float1, {fromNode("texcoord", VT::vector2, 1), value("jitter", cp::Value::makeFloat(0.0f))}),
    });
    const auto baked = cp::bake(graph, size(8, 8));
    ASSERT_TRUE(baked.ok()) << baked.error;
    // Texel (1, 1): (0.1875, 0.8125) * 2 = (0.375, 1.625); nearest centre (0.5, 1.5).
    EXPECT_NEAR(at(baked.images[0], 1, 1)[0], std::sqrt(0.03125f), 1e-5f);
    // Texel (2, 2): (0.3125, 0.6875) * 2 = (0.625, 1.375); nearest centre (0.5, 1.5).
    EXPECT_NEAR(at(baked.images[0], 2, 2)[0], std::sqrt(0.03125f), 1e-5f);
    // Texel (0, 0): (0.125, 1.875); nearest centre (0.5, 1.5), 0.375 away on each axis.
    EXPECT_NEAR(at(baked.images[0], 0, 0)[0], std::sqrt(0.28125f), 1e-5f);
}

TEST(Bake, Place2dScalesTheCoordinates) {
    // A checkerboard of 2 x 2 squares, through place2d scaling by 0.5: 4 x 4.
    const auto plain = cp::bake(single({node("checkerboard", VT::color3, {value("uvtiling", cp::Value::makeVector2(4, 4))})}),
                                size(16, 16));
    const auto placed = cp::bake(
        single({node("place2d", VT::vector2, {value("scale", cp::Value::makeVector2(0.5f, 0.5f))}),
                node("checkerboard", VT::color3,
                     {value("uvtiling", cp::Value::makeVector2(2, 2)), fromNode("texcoord", VT::vector2, 0)})}),
        size(16, 16));
    ASSERT_TRUE(plain.ok() && placed.ok()) << plain.error << placed.error;
    EXPECT_EQ(plain.images[0].rgba, placed.images[0].rgba);
}

TEST(Bake, ThreadsDontChangeTheResult) {
    const auto graph = single({node("fractal2d", VT::color3, {value("octaves", cp::Value::makeInteger(5))})});
    auto one = size(37, 23);
    one.threads = 1;
    auto many = one;
    many.threads = 8;
    const auto a = cp::bake(graph, one);
    const auto b = cp::bake(graph, many);
    ASSERT_TRUE(a.ok() && b.ok());
    EXPECT_EQ(a.images[0].rgba, b.images[0].rgba);
}

TEST(Bake, ImagesComeFromTheResolver) {
    auto image = std::make_shared<cp::Image>();
    image->width = 2;
    image->height = 1;
    image->rgba = {1, 0, 0, 1, 0, 0, 1, 1};  // red, blue
    cp::Graph graph = single({node("image", VT::color3,
                                   {value("file", cp::Value::makeFilename("bricks.png")),
                                    value("default", cp::Value::makeColor3(0, 1, 0)),
                                    value("filtertype", cp::Value::makeString("closest"))})});
    auto options = size(2, 1);
    const auto fallback = cp::bake(graph, options);
    ASSERT_TRUE(fallback.ok()) << fallback.error;
    EXPECT_EQ(at(fallback.images[0], 0, 0)[1], 1.0f) << "no resolver: the default";
    std::string asked;
    options.images = [&](const cp::ImageRef& ref) {
        asked = ref.filename;
        return image;
    };
    const auto sampled = cp::bake(graph, options);
    ASSERT_TRUE(sampled.ok());
    EXPECT_EQ(asked, "bricks.png");
    EXPECT_EQ(at(sampled.images[0], 0, 0)[0], 1.0f);
    EXPECT_EQ(at(sampled.images[0], 1, 0)[2], 1.0f);
}

TEST(Bake, CompileErrorsNameTheProblem) {
    std::string error;
    // A cycle.
    cp::Graph cycle = single({node("add", VT::float1, {fromNode("in1", VT::float1, 1)}),
                              node("add", VT::float1, {fromNode("in1", VT::float1, 0)})});
    EXPECT_EQ(cp::Program::compile(cycle, &error), nullptr);
    EXPECT_NE(error.find("cycle"), std::string::npos) << error;
    // An unknown node.
    EXPECT_EQ(cp::Program::compile(single({node("brickwall", VT::color3)}), &error), nullptr);
    EXPECT_NE(error.find("brickwall"), std::string::npos) << error;
    // An unknown input.
    EXPECT_EQ(cp::Program::compile(single({node("add", VT::float1, {value("in3", cp::Value::makeFloat(1))})}), &error),
              nullptr);
    EXPECT_NE(error.find("in3"), std::string::npos) << error;
    // A type mismatch: a vector2 into a color3 input.
    EXPECT_EQ(cp::Program::compile(single({node("texcoord", VT::vector2),
                                           node("add", VT::color3, {fromNode("in1", VT::vector2, 0)})}),
                                   &error),
              nullptr);
    EXPECT_NE(error.find("components"), std::string::npos) << error;
    // A connection to a node that doesn't exist.
    EXPECT_EQ(cp::Program::compile(single({node("add", VT::float1, {fromNode("in1", VT::float1, 9)})}), &error), nullptr);
    // A float broadcasts into a colour.
    EXPECT_NE(cp::Program::compile(single({node("constant", VT::float1),
                                           node("add", VT::color3, {fromNode("in1", VT::float1, 0)})}),
                                   &error),
              nullptr)
        << error;
}

TEST(Encode, SrgbAndLinear) {
    cp::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {0.5f, 0.0f, 1.5f, 0.5f};
    const auto srgb = cp::toRGBA8(image, cp::Encoding::srgb);
    EXPECT_EQ(srgb[0], 188);
    EXPECT_EQ(srgb[1], 0);
    EXPECT_EQ(srgb[2], 255) << "clamped";
    EXPECT_EQ(srgb[3], 128) << "alpha stays linear";
    EXPECT_EQ(cp::toRGBA8(image, cp::Encoding::linear)[0], 128);
}

TEST(Encode, MipChains) {
    cp::Image image;
    image.width = 4;
    image.height = 2;
    image.rgba.assign(4 * 2 * 4, 0.0f);
    image.texel(0, 0)[0] = 1.0f;  // one red texel in the first 2x2 block
    const auto chain = cp::mipChain(image);
    ASSERT_EQ(chain.size(), 2u);
    EXPECT_EQ(chain[0].width, 2u);
    EXPECT_EQ(chain[0].height, 1u);
    EXPECT_EQ(chain[1].width, 1u);
    EXPECT_FLOAT_EQ(chain[0].texel(0, 0)[0], 0.25f);
    EXPECT_FLOAT_EQ(chain[1].texel(0, 0)[0], 0.125f);

    // Normals average to unit length again.
    cp::Image normals;
    normals.width = 2;
    normals.height = 2;
    normals.rgba = {1.0f, 0.5f, 0.5f, 1, 0.0f, 0.5f, 0.5f, 1, 0.5f, 0.5f, 1.0f, 1, 0.5f, 0.5f, 1.0f, 1};
    const auto level = cp::mipChain(normals, cp::MipFilter::normal).at(0);
    const float* n = level.texel(0, 0);
    const float x = n[0] * 2 - 1, y = n[1] * 2 - 1, z = n[2] * 2 - 1;
    EXPECT_NEAR(x * x + y * y + z * z, 1.0f, 1e-5f);
    EXPECT_NEAR(z, 1.0f, 1e-5f);
}
