#pragma once

#include <campello_procedural/value.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace systems::leal::campello_procedural {

/// Where an input's value comes from when nothing is connected or set.
enum class DefaultSource : uint8_t {
    value,     ///< NodeInputDef::defaultValue
    texcoord,  ///< the bake's texture coordinates (MaterialX: texcoord index 0)
    position,  ///< the bake's position: (u, v, 0)
};

/// One input of a node definition.
struct NodeInputDef {
    std::string name;
    ValueType type = ValueType::none;
    Value defaultValue;
    DefaultSource source = DefaultSource::value;
};

/// One output of a node definition (several for multioutput nodes such as
/// separate3).
struct NodeOutputDef {
    std::string name;
    ValueType type = ValueType::none;
};

/// The signature of a node category for one output type: its inputs with
/// their defaults (MaterialX 1.39 standard library) and its outputs.
struct NodeDef {
    std::string category;
    ValueType type = ValueType::none;  ///< the node's type (multioutput for several outputs)
    std::string group;                 ///< MaterialX node group: "math", "procedural2d", ...
    std::vector<NodeInputDef> inputs;
    std::vector<NodeOutputDef> outputs;
};

/// The categories the evaluator supports, sorted.
std::vector<std::string> nodeCategories();

/// The definition of `category` producing `type`, or false when the
/// category is unknown or doesn't produce that type. For multioutput nodes
/// (separate2/3/4), `type` is the type separated.
bool findNodeDef(std::string_view category, ValueType type, NodeDef& out);

/// Every output type `category` can produce.
std::vector<ValueType> nodeTypes(std::string_view category);

}  // namespace systems::leal::campello_procedural
