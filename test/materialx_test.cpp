#include "test_util.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace test_util;
using VT = cp::ValueType;

namespace {

bool hasWarning(const cp::MaterialXResult& result, std::string_view part) {
    return std::any_of(result.warnings.begin(), result.warnings.end(),
                       [&](const std::string& w) { return w.find(part) != std::string::npos; });
}

}  // namespace

// MaterialX's marble: a node graph with interface inputs feeding a
// standard_surface's base colour; constant roughness.
TEST(MaterialX, ReadsTheMarble) {
    const auto result = cp::readMaterialX(readFile("marble.mtlx"));
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_TRUE(result.warnings.empty()) << result.warnings.front();
    ASSERT_EQ(result.graphs.size(), 1u);
    const auto& graph = result.graphs[0];
    EXPECT_EQ(graph.name, "Marble_3D");
    // Slots: base_color, roughness, and a flat normal (the shader has none).
    EXPECT_GE(graph.findOutput("base_color"), 0);
    EXPECT_GE(graph.findOutput("roughness"), 0);
    EXPECT_GE(graph.findOutput("normal"), 0);
    EXPECT_LT(graph.findOutput("metallic"), 0) << "the shader doesn't set it";
    // The node graph's interface inputs, with their UI data.
    ASSERT_EQ(graph.inputs.size(), 6u);
    const auto* power = graph.findInput("noise_power");
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->uiName, "Power");
    EXPECT_EQ(power->uiFolder, "Marble Noise");
    EXPECT_EQ(power->uiSoftMax, 10.0f);
    EXPECT_EQ(graph.findInput("noise_octaves")->type, VT::integer);

    auto options = size(32, 32);
    const auto baked = cp::bake(graph, options);
    ASSERT_TRUE(baked.ok()) << baked.error;
    // Veins: colour 2 (dark blue) mixed into colour 1 (light grey).
    float darkest = 1.0f, lightest = 0.0f;
    for (size_t i = 0; i < baked.find("base_color")->rgba.size(); i += 4) {
        darkest = std::min(darkest, baked.find("base_color")->rgba[i]);
        lightest = std::max(lightest, baked.find("base_color")->rgba[i]);
    }
    EXPECT_LT(darkest, 0.3f);
    EXPECT_GT(lightest, 0.7f);
    EXPECT_FLOAT_EQ(baked.find("roughness")->rgba[0], 0.1f);
    const float* flat = baked.find("normal")->texel(3, 3);
    EXPECT_FLOAT_EQ(flat[0], 0.5f);
    EXPECT_FLOAT_EQ(flat[2], 1.0f);

    // Its inputs are parameters: other colours, other image.
    options.inputs["base_color_2"] = cp::Value::makeColor3(0.9f, 0.1f, 0.1f);
    const auto red = cp::bake(graph, options);
    ASSERT_TRUE(red.ok());
    EXPECT_NE(red.find("base_color")->rgba, baked.find("base_color")->rgba);

    // And it survives the glTF JSON.
    const auto again = cp::readProcedurals(cp::writeGraph(graph));
    ASSERT_TRUE(again.ok()) << again.error;
    const auto rebaked = cp::bake(again.graphs[0], size(32, 32));
    ASSERT_TRUE(rebaked.ok());
    EXPECT_EQ(rebaked.find("base_color")->rgba, baked.find("base_color")->rgba);
}

// Top-level nodes, gltf_pbr, an sRGB value, a normal map over a height, an
// image (not read) and an unknown node (a warning, a constant instead).
TEST(MaterialX, ShadersNormalMapsAndWarnings) {
    const char* document = R"(<?xml version="1.0"?>
<materialx version="1.39">
  <!-- tiles -->
  <texcoord name="uv" type="vector2" />
  <multiply name="scaled" type="vector2">
    <input name="in1" type="vector2" nodename="uv" />
    <input name="in2" type="vector2" value="4, 4" />
  </multiply>
  <checkerboard name="board" type="color3">
    <input name="color1" type="color3" value="1, 0.5, 0" colorspace="srgb_texture" />
    <input name="color2" type="color3" value="0, 0, 0" />
    <input name="uvtiling" type="vector2" value="1, 1" />
    <input name="texcoord" type="vector2" nodename="scaled" />
  </checkerboard>
  <noise2d name="bumps" type="float">
    <input name="texcoord" type="vector2" nodename="scaled" />
  </noise2d>
  <heighttonormal name="height" type="vector3">
    <input name="in" type="float" nodename="bumps" />
    <input name="scale" type="float" value="0.05" />
  </heighttonormal>
  <normalmap name="mapped" type="vector3">
    <input name="in" type="vector3" nodename="height" />
  </normalmap>
  <image name="photo" type="float">
    <input name="file" type="filename" value="rough.png" />
  </image>
  <hexagonaltile name="odd" type="float" />
  <gltf_pbr name="pbr" type="surfaceshader">
    <input name="base_color" type="color3" nodename="board" />
    <input name="normal" type="vector3" nodename="mapped" />
    <input name="roughness" type="float" nodename="photo" />
    <input name="metallic" type="float" nodename="odd" />
  </gltf_pbr>
</materialx>)";
    const auto result = cp::readMaterialX(document);
    ASSERT_TRUE(result.ok()) << result.error;
    ASSERT_EQ(result.graphs.size(), 1u) << "a shader without a material";
    const auto& graph = result.graphs[0];
    EXPECT_EQ(graph.name, "pbr");
    EXPECT_TRUE(hasWarning(result, "rough.png"));
    EXPECT_TRUE(hasWarning(result, "hexagonaltile"));
    // The normal map's input stands for it: the height's normal.
    const int normal = graph.findOutput("normal");
    ASSERT_GE(normal, 0);
    EXPECT_EQ(graph.nodes[size_t(graph.outputs[size_t(normal)].node)].category, "heighttonormal");

    const auto baked = cp::bake(graph, size(16, 16));
    ASSERT_TRUE(baked.ok()) << baked.error;
    // The sRGB orange became linear: 0.5 -> about 0.214.
    float green = 0.0f;
    for (size_t i = 0; i < baked.find("base_color")->rgba.size(); i += 4) green = std::max(green, baked.find("base_color")->rgba[i + 1]);
    EXPECT_NEAR(green, 0.214f, 0.002f);
    EXPECT_FLOAT_EQ(baked.find("metallic")->rgba[0], 0.0f) << "the unknown node is a constant";
}

TEST(MaterialX, ReportsWhatItCantRead) {
    EXPECT_FALSE(cp::readMaterialX("").ok());
    EXPECT_FALSE(cp::readMaterialX("<materialx><nodegraph name=\"x\"></materialx>").ok()) << "unclosed";
    EXPECT_FALSE(cp::readMaterialX("<notmaterialx/>").ok());
    const auto noShader = cp::readMaterialX(R"(<materialx version="1.39">
      <disney_principled name="d" type="surfaceshader"/>
      <surfacematerial name="m" type="material"><input name="surfaceshader" type="surfaceshader" nodename="d"/></surfacematerial>
    </materialx>)");
    EXPECT_FALSE(noShader.ok());
    EXPECT_TRUE(hasWarning(noShader, "disney_principled"));
    // Entities in attributes.
    const auto entities = cp::readMaterialX(R"(<materialx version="1.39">
      <nodegraph name="g">
        <input name="tint" type="color3" value="0.5, 0.5, 0.5" uiname="Tint &amp; shade" />
        <constant name="c" type="color3"><input name="value" type="color3" interfacename="tint"/></constant>
        <output name="out" type="color3" nodename="c"/>
      </nodegraph>
      <UsdPreviewSurface name="s" type="surfaceshader"><input name="diffuseColor" type="color3" nodegraph="g" output="out"/></UsdPreviewSurface>
    </materialx>)");
    ASSERT_TRUE(entities.ok()) << entities.error;
    EXPECT_EQ(entities.graphs[0].inputs[0].uiName, "Tint & shade");
}
