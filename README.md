# campello_procedural

Procedural texture graphs for the Campello libraries: graphs of [MaterialX](https://materialx.org) 1.39 standard library nodes, in the JSON of the draft glTF extension [`KHR_texture_procedurals`](https://github.com/KhronosGroup/glTF/pull/2381), baked to images on the CPU.

A graph has inputs (the parameters a material sets: brick size, colours, noise scale), nodes, and outputs. One graph usually feeds several material slots, so a pattern's base colour, normal and roughness come from the same nodes and stay aligned.

- **Graph model** (`graph.hpp`): graphs, nodes, ports and connections.
- **JSON** (`json.hpp`): reads a glTF document, the extension object or a single graph. It accepts both layouts the draft shows: ports keyed by name with connections by name (the README's examples), or arrays of ports with connections by index (its schema). It writes the first.
- **Node library** (`nodes.hpp`): the supported nodes with their MaterialX inputs and defaults, for validation and for node editors.
- **Bake** (`bake.hpp`): compiles a graph to a flat program (type checked, cycles rejected) and evaluates every output in one pass on all cores. It also encodes RGBA8 (linear or sRGB) and builds mip chains (box, or renormalized for normal maps).

No dependencies beyond the C++20 standard library. nlohmann/json is bundled privately.

## Example

```cpp
#include <campello_procedural/campello_procedural.hpp>
namespace cp = systems::leal::campello_procedural;

auto read = cp::readProcedurals(gltfJson);           // a glTF with KHR_texture_procedurals
auto program = cp::Program::compile(read.graphs[0]); // validate once
cp::BakeOptions options;
options.width = options.height = 1024;
options.inputs["brick_color"] = cp::Value::makeColor3(0.61f, 0.29f, 0.19f);
auto baked = cp::bake(*program, options);            // every output, one pass
auto baseColor = cp::toRGBA8(*baked.find("base_color"), cp::Encoding::srgb);
auto mips = cp::mipChain(*baked.find("normal"), cp::MipFilter::normal);
```

## Conventions

- **Coordinates.** Graphs see MaterialX texture coordinates: u to the right, v up. Row 0 of a baked image is v = 1, so the image is a glTF texture as it is (glTF's v grows downwards).
- **3D position.** The bake's `position` is (u, v, 0).
- **Normals.** `heighttonormal` writes tangent-space normals with +y up, encoded 0..1. Its `scale` multiplies the height's slope per unit of texture coordinate.
- **Noise.** The noise functions (`noise2d/3d`, `fractal2d/3d`, `cellnoise2d/3d`, `worleynoise2d/3d`) are ported from MaterialX's `mx_noise.glsl`.

## Supported nodes

- **Math:**
  - Arithmetic: `add` `subtract` `multiply` `divide` `modulo` `power` `safepower` `min` `max`
  - Unary: `absval` `sign` `floor` `ceil` `round` `sqrt` `exp` `ln` `invert`
  - Trigonometry: `sin` `cos` `tan` `asin` `acos` `atan2`
  - Ranges: `clamp` `smoothstep` `remap`
  - Vectors: `magnitude` `distance` `dotproduct` `normalize` `crossproduct`
  - Coordinates: `rotate2d` `place2d`
- **Compositing and conditionals:** `mix` `ifgreater` `ifgreatereq` `ifequal`
- **Channels:** `combine2/3/4` `separate2/3/4` `extract` `convert` `swizzle` (1.38) `constant` `dot`
- **Colour:** `luminance` `saturate` `contrast` `hsvtorgb` `rgbtohsv`
- **Patterns:** `checkerboard` `ramplr` `ramptb` `ramp4` `splitlr` `splittb`
- **Noise:**
  - Perlin: `noise2d` `noise3d`
  - Fractal: `fractal2d` `fractal3d`
  - Cell: `cellnoise2d` `cellnoise3d`
  - Worley (F1, Euclidean): `worleynoise2d` `worleynoise3d`
- **Geometry:** `texcoord` `position`
- **Normals:** `heighttonormal`
- **Images:** `image` and `tiledimage`, through an `ImageResolver` callback.

Not yet: matrices, booleans as logic nodes, `normalmap` (it needs a surface's tangent frame), Worley's F2/F3 and distance metrics, `unifiednoise`, the tiled shape nodes.

## Build

```bash
cmake -S . -B build -DCAMPELLO_PROCEDURAL_BUILD_TEST=ON
cmake --build build
ctest --test-dir build
```

Consumed through CMake `FetchContent` like the other Campello libraries.

## License

Apache License 2.0 (`LICENSE`), the same as MaterialX. See `NOTICE` for the code ported from MaterialX and the bundled nlohmann/json.
