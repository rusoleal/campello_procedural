# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

`campello_procedural` (v0.3.0) is a C++20 static library for procedural texture graphs:
- MaterialX 1.39 standard library nodes.
- The JSON of the draft glTF extension `KHR_texture_procedurals` (KhronosGroup/glTF#2381; a draft, still changing).
- Baked to float images on the CPU.

It has no dependencies; nlohmann/json is bundled in `src/nlohmann_json.hpp` and stays private.

Consumers:
- **campello_renderer:** bakes glTF materials' KHR_texture_procedurals graphs with it (`uploadMaterial()`, `setScene()`).
- **gltf:** converts its parsed extension to and from these graphs.
- **campello_bim:** its materials embed graphs and bake them for the 3D view.

## Build and test

```bash
cmake -S . -B build/debug -DCAMPELLO_PROCEDURAL_BUILD_TEST=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug        # or ./build/debug/campello_procedural_test
```

## Layout

| File | Contents |
|------|----------|
| `inc/campello_procedural/value.hpp`, `src/value.cpp` | `ValueType` (MaterialX type names), `Value`. Graph/Node lookups also live in `value.cpp`. |
| `graph.hpp` | `Graph`, `Node`, `Input`, `Output`. Connections are by node index plus output name, or by graph input name. |
| `json.hpp`, `src/json.cpp` | Reads both of the draft's layouts and writes the README layout (ports keyed by name). Uses `nlohmann::ordered_json` so port order survives. |
| `materialx.hpp`, `src/materialx.cpp` | `readMaterialX()`: a small XML reader (elements and attributes only), then `Converter` builds one graph per surface material from its shader's inputs (`shaderSlots()`), following connections through node graphs and top-level nodes. Node graphs' interface inputs with values become graph inputs, and unknown nodes become constants with a warning. |
| `nodes.hpp`, `src/nodes.cpp` | The node registry: category -> type -> `NodeDef` (inputs with MaterialX defaults, outputs). Inputs with `ValueType::none` take any numeric type. `DefaultSource` marks texcoord/position defaults. |
| `bake.hpp`, `src/program.cpp` | `Program::compile()` (`Compiler`: depth-first from the graph outputs, one instruction per node, registers of 4 floats; a float source broadcasts into a vector input) and `bake()`, plus RGBA8 encoding and mip chains. |
| `src/noise.hpp` | The MaterialX noise port. Keep it faithful to `mx_noise.glsl`, so graphs bake the same as in MaterialX. |

## Conventions

- **Coordinates are MaterialX's:** v up, and texel row 0 is v = 1, so baked images are glTF textures as they are. Ramps and splits follow that: `valuet` is at the top, v = 1.
- **Registers:** 0 holds texcoord and 1 holds position, both (u, v, 0). Then come constants and graph inputs (set per bake), then node outputs. `separateN` writes consecutive registers.
- **`heighttonormal`, read only by graph outputs** (`Instr::deferred`, set by `Program::compile()`): `bake()` evaluates on a grid one texel wider, stores the height, and differences it after the pass.
- **`heighttonormal`, read by another node:** re-runs the instructions its height depends on (`Instr::cone`) at ±1 texel, then runs them again at the centre so later readers see centre values. `DeferredNormalsMatchReevaluatedOnes` keeps the two paths equal.
- **Adding a node:**
  1. Define it in `nodes.cpp`.
  2. Add an `Op` with its `opTable()` entry, plus its `exec()` case reading operands in definition order.
  3. Cover it in the tests. `Nodes.EveryNodeBakesWithItsDefaults` already bakes every definition.

## Versioning and releases

The version lives in `CMakeLists.txt` (`project(... VERSION x.y.z)`) and in `test/json_test.cpp` (`Version.MatchesTheProject`). To release:
1. Add a `## [x.y.z] - YYYY-MM-DD` entry to `CHANGELOG.md`.
2. Bump both places and run the tests.
3. Commit and push.
4. `gh release create vX.Y.Z` with a short summary and a link to the changelog entry.
