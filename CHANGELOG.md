# Changelog

## [0.1.0] - 2026-11-19

### Added
- **Graph model** for procedural textures (`graph.hpp`): graphs with interface inputs (with UI descriptions), nodes, outputs and connections, following the draft glTF extension `KHR_texture_procedurals` (KhronosGroup/glTF#2381).
- **JSON read and write** (`json.hpp`). Reads a glTF document, the extension object or a single graph, in either of the draft's two layouts: ports keyed by name with connections by name, or port arrays with connections by index. Writes the first. Reads the draft's checkerboard example.
- **Node library** (`nodes.hpp`): about 70 MaterialX 1.39 standard library nodes with their inputs and defaults, covering math, channels, colour adjustment, patterns, Perlin/fractal/cell/Worley noise in 2D and 3D, `place2d`, `heighttonormal`, `image` and `tiledimage`. Also accepts MaterialX 1.38's `swizzle` and `length`.
- **Bake** (`bake.hpp`):
  - `Program::compile()` type checks a graph, rejects cycles and unknown nodes or inputs, and flattens it to instructions.
  - `bake()` evaluates every output in one pass on all cores, with interface input overrides and images from a resolver callback. Results don't depend on the thread count.
  - Graphs see MaterialX coordinates (v up), and baked images are glTF textures as they are.
  - `heighttonormal` differentiates the height in texture coordinates, so its normals don't depend on the bake size.
  - About 400 ms for a 1024x1024 bake of three outputs from a 5-octave fractal, on a 6-core Intel Mac.
- **Output helpers:** `toRGBA8()` (linear or sRGB) and `mipChain()` (box, or renormalized for normal maps).
- **Noise ported from MaterialX's `mx_noise.glsl`** (Apache 2.0, see `NOTICE`).
- 25 tests.
