#pragma once

// campello_procedural: procedural texture graphs (KHR_texture_procedurals,
// MaterialX 1.39 standard library nodes), read and written as JSON, read
// from MaterialX documents, and baked to images on the CPU.

#include <campello_procedural/bake.hpp>
#include <campello_procedural/graph.hpp>
#include <campello_procedural/json.hpp>
#include <campello_procedural/materialx.hpp>
#include <campello_procedural/nodes.hpp>
#include <campello_procedural/value.hpp>

#include <string>

namespace systems::leal::campello_procedural {

/// The library version, e.g. "0.3.0".
std::string getVersion();

}  // namespace systems::leal::campello_procedural
