#pragma once

#include <campello_procedural/graph.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace systems::leal::campello_procedural {

/// A float RGBA image, rows top to bottom (row 0 at v = 0, as glTF texture
/// coordinates), 4 floats per texel.
struct Image {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<float> rgba;

    const float* texel(uint32_t x, uint32_t y) const { return rgba.data() + (size_t(y) * width + x) * 4; }
    float* texel(uint32_t x, uint32_t y) { return rgba.data() + (size_t(y) * width + x) * 4; }
};

/// What an `image` node (or a graph input bound to a glTF texture) reads.
struct ImageRef {
    std::string filename;  ///< the node's "file" input, if set
    int texture = -1;      ///< a glTF texture index, if bound to one
};

/// Supplies images to `image` nodes: linear RGBA (decode sRGB colour
/// images first). Null: the node gives its `default` input.
using ImageResolver = std::function<std::shared_ptr<const Image>(const ImageRef&)>;

class Program;

/// A graph compiled for evaluation: its nodes in dependency order as flat
/// instructions on registers. Compile once, bake many times (with other
/// input values, sizes or outputs). Immutable; bakes may run concurrently.
class Program {
public:
    /// Compiles a graph, or returns null and the reason (unknown node or
    /// input, type mismatch, missing connection, cycle...).
    static std::shared_ptr<const Program> compile(const Graph& graph, std::string* error = nullptr);

    ~Program();
    const Graph& graph() const;
    struct Impl;
    const Impl& impl() const { return *impl_; }

private:
    explicit Program(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

struct BakeOptions {
    uint32_t width = 1024;
    uint32_t height = 1024;
    /// The graph outputs to bake, by name; empty: all of them.
    std::vector<std::string> outputs;
    /// Values for the graph's interface inputs, replacing theirs (the
    /// parameters a material sets: brick size, colours...).
    std::map<std::string, Value> inputs;
    ImageResolver images;
    /// Worker threads; 0: the hardware's.
    uint32_t threads = 0;
};

/// The baked outputs, in the order asked for (or the graph's order).
struct BakeResult {
    std::vector<std::string> names;
    std::vector<Image> images;
    std::string error;  ///< empty on success

    bool ok() const { return error.empty(); }
    /// The image of an output, or null.
    const Image* find(std::string_view name) const;
};

/// Evaluates the program at every texel centre for every output in one
/// pass, so nodes shared by several outputs (a pattern's mask, a height
/// field) are computed once per texel. Outputs fill RGBA as: float
/// (v, v, v, 1), vector2 (x, y, 0, 1), vector3/color3 (x, y, z, 1),
/// vector4/color4 as they are.
///
/// Graphs see MaterialX texture coordinates, u to the right and v up:
/// texel (x, y) is at u = (x + 0.5) / width, v = 1 - (y + 0.5) / height, so
/// row 0 (the top) is v = 1. The image is then a glTF texture as it is
/// (glTF's v grows downwards: v_glTF = 1 - v_MaterialX). `position` is
/// (u, v, 0). heighttonormal writes tangent-space normals with +y up (glTF's
/// and MaterialX's convention), encoded 0..1.
BakeResult bake(const Program& program, const BakeOptions& options);

/// Compiles and bakes in one call.
BakeResult bake(const Graph& graph, const BakeOptions& options);

/// How an image is stored as 8 bits per channel.
enum class Encoding : uint8_t {
    linear,  ///< values clamped to 0..1
    srgb,    ///< colour channels sRGB-encoded (base colour, emissive), alpha linear
};

/// The image as RGBA8.
std::vector<uint8_t> toRGBA8(const Image& image, Encoding encoding);

/// How mip levels are averaged.
enum class MipFilter : uint8_t {
    box,     ///< 2x2 average (in linear values)
    normal,  ///< 2x2 average of 0..1-encoded normals, renormalized
};

/// The mip chain below an image (half size each, down to 1x1; odd sizes
/// round down), not including the image itself.
std::vector<Image> mipChain(const Image& image, MipFilter filter = MipFilter::box);

}  // namespace systems::leal::campello_procedural
