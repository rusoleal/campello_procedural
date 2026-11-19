#pragma once

#include <campello_procedural/graph.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace systems::leal::campello_procedural {

/// Graphs read from a MaterialX document, one per material, or why reading
/// failed.
struct MaterialXResult {
    std::vector<Graph> graphs;
    /// What was left out: nodes or shader inputs that can't be used, images
    /// (their files aren't read here), and so on. The graphs still bake.
    std::vector<std::string> warnings;
    std::string error;  ///< empty on success

    bool ok() const { return error.empty(); }
};

/// Reads a MaterialX document (.mtlx, versions 1.38 and 1.39): for each
/// surface material (or each surface shader when there are no materials), a
/// graph whose outputs are the material's texture slots, named as glTF's:
///   base_color (color3), normal (vector3: tangent space, +y up, encoded
///   0..1), roughness (float), metallic (float), emissive (color3).
/// Only the slots the shader uses are there; normal always is (flat when the
/// shader has none). The graph is named after the material.
///
/// Shaders: standard_surface, gltf_pbr, open_pbr_surface and
/// UsdPreviewSurface. A slot's value comes from what its shader input
/// connects to: a node graph's output, a top-level node, or a constant. A
/// `normalmap` feeding the normal is replaced by its tangent-space input.
/// Node graphs' inputs that carry a value become the graph's inputs (with
/// their UI names and ranges), so the material's parameters stay editable.
/// Values in sRGB (colorspace "srgb_texture") are converted to linear.
///
/// Not supported (warnings): custom node definitions and functional node
/// graphs, image files (image nodes keep their default), shaders other than
/// the four above.
MaterialXResult readMaterialX(std::string_view document);

}  // namespace systems::leal::campello_procedural
