#include <campello_procedural/json.hpp>

#include "nlohmann_json.hpp"

namespace systems::leal::campello_procedural {

namespace {

using Json = nlohmann::ordered_json;

// A port's name: the key it's stored under (README layout) or its "name"
// (schema layout).
std::string portName(const std::string& key, const Json& port) {
    if (port.contains("name") && port["name"].is_string()) return port["name"].get<std::string>();
    return key;
}

bool readValue(const Json& json, ValueType type, Value& out, std::string& error) {
    out = Value{};
    out.type = type;
    if (type == ValueType::string || type == ValueType::filename) {
        if (!json.is_string()) {
            error = "a string value isn't a string";
            return false;
        }
        out.text = json.get<std::string>();
        return true;
    }
    if (json.is_boolean()) {
        out.data[0] = json.get<bool>() ? 1.0f : 0.0f;
        return true;
    }
    if (json.is_number()) {
        out.data[0] = json.get<float>();
        return true;
    }
    if (json.is_array()) {
        if (json.size() > 16) {
            error = "a value has more than 16 components";
            return false;
        }
        for (size_t i = 0; i < json.size(); ++i) {
            if (!json[i].is_number()) {
                error = "a value array holds a non-number";
                return false;
            }
            out.data[i] = json[i].get<float>();
        }
        return true;
    }
    error = "unsupported value";
    return false;
}

Json writeValue(const Value& value) {
    if (value.type == ValueType::string || value.type == ValueType::filename) return value.text;
    if (value.type == ValueType::boolean) return value.data[0] != 0.0f;
    if (value.type == ValueType::integer) return int(value.data[0]);
    const int n = componentCount(value.type);
    if (n <= 1) return value.data[0];
    Json array = Json::array();
    for (int i = 0; i < n; ++i) array.push_back(value.data[size_t(i)]);
    return array;
}

bool readType(const Json& port, ValueType& type, std::string& error) {
    if (!port.contains("type") || !port["type"].is_string()) {
        error = "a port has no type";
        return false;
    }
    const auto name = port["type"].get<std::string>();
    const auto parsed = valueTypeFromString(name);
    if (!parsed) {
        error = "unknown type \"" + name + "\"";
        return false;
    }
    type = *parsed;
    return true;
}

// Ports: an object keyed by name, or an array of named ports.
template <typename F>
bool forEachPort(const Json& ports, F&& f) {
    if (ports.is_object()) {
        for (auto it = ports.begin(); it != ports.end(); ++it) {
            if (!f(it.key(), it.value())) return false;
        }
    } else if (ports.is_array()) {
        for (size_t i = 0; i < ports.size(); ++i) {
            if (!f(std::string(), ports[i])) return false;
        }
    }
    return true;
}

void readUi(const Json& port, Input& input) {
    const auto text = [&](const char* key, std::string& out) {
        if (port.contains(key) && port[key].is_string()) out = port[key].get<std::string>();
    };
    const auto number = [&](const char* key, std::optional<float>& out) {
        if (port.contains(key) && port[key].is_number()) out = port[key].get<float>();
    };
    text("uiname", input.uiName);
    text("uifolder", input.uiFolder);
    text("doc", input.doc);
    number("uimin", input.uiMin);
    number("uimax", input.uiMax);
    number("uisoftmin", input.uiSoftMin);
    number("uisoftmax", input.uiSoftMax);
    number("uistep", input.uiStep);
}

// Connections by index (schema layout) are resolved to names once every
// port is known, so these hold them in the meantime.
struct PendingIndex {
    Input* input = nullptr;
    int graphInputIndex = -1;  // "input": <index into the graph's inputs>
    int nodeOutputIndex = -1;  // "output": <index into the node's outputs>
};

bool readInput(const std::string& key, const Json& port, Input& input, std::vector<PendingIndex>& pending,
               std::string& error) {
    if (!port.is_object()) {
        error = "an input isn't an object";
        return false;
    }
    input.name = portName(key, port);
    if (!readType(port, input.type, error)) {
        error = "input \"" + input.name + "\": " + error;
        return false;
    }
    PendingIndex index;
    if (port.contains("value")) {
        Value value;
        if (!readValue(port["value"], input.type, value, error)) {
            error = "input \"" + input.name + "\": " + error;
            return false;
        }
        input.value = value;
    }
    if (port.contains("node") && port["node"].is_number_integer()) {
        input.node = port["node"].get<int>();
        if (port.contains("output")) {
            if (port["output"].is_string()) input.output = port["output"].get<std::string>();
            else if (port["output"].is_number_integer()) index.nodeOutputIndex = port["output"].get<int>();
        }
    }
    if (port.contains("input")) {
        if (port["input"].is_string()) input.graphInput = port["input"].get<std::string>();
        else if (port["input"].is_number_integer()) index.graphInputIndex = port["input"].get<int>();
    }
    if (port.contains("texture") && port["texture"].is_number_integer()) input.texture = port["texture"].get<int>();
    readUi(port, input);
    if (index.graphInputIndex >= 0 || index.nodeOutputIndex >= 0) {
        index.input = &input;
        pending.push_back(index);
    }
    return true;
}

bool readOutput(const std::string& key, const Json& port, Output& output, std::string& error) {
    if (!port.is_object()) {
        error = "an output isn't an object";
        return false;
    }
    output.name = portName(key, port);
    if (!readType(port, output.type, error)) {
        error = "output \"" + output.name + "\": " + error;
        return false;
    }
    if (port.contains("node") && port["node"].is_number_integer()) output.node = port["node"].get<int>();
    if (port.contains("output") && port["output"].is_string()) output.output = port["output"].get<std::string>();
    return true;
}

bool readGraph(const Json& json, Graph& graph, std::string& error) {
    if (!json.is_object()) {
        error = "a procedural isn't an object";
        return false;
    }
    if (json.contains("name") && json["name"].is_string()) graph.name = json["name"].get<std::string>();
    if (!readType(json, graph.type, error)) {
        error = "graph \"" + graph.name + "\": " + error;
        return false;
    }
    // Inputs and nodes are sized up front: pending index connections point
    // into them.
    const auto count = [](const Json& ports) { return ports.is_object() || ports.is_array() ? ports.size() : 0; };
    std::vector<PendingIndex> pending;
    // Outputs whose connection to a node output is by index (graph outputs).
    std::vector<std::pair<Output*, int>> pendingOutputs;
    if (json.contains("inputs")) {
        graph.inputs.reserve(count(json["inputs"]));
        const bool ok = forEachPort(json["inputs"], [&](const std::string& key, const Json& port) {
            graph.inputs.emplace_back();
            return readInput(key, port, graph.inputs.back(), pending, error);
        });
        if (!ok) return false;
    }
    if (json.contains("nodes") && json["nodes"].is_array()) {
        const auto& nodes = json["nodes"];
        graph.nodes.resize(nodes.size());
        for (size_t n = 0; n < nodes.size(); ++n) {
            const auto& nodeJson = nodes[n];
            auto& node = graph.nodes[n];
            if (!nodeJson.is_object()) {
                error = "a node isn't an object";
                return false;
            }
            if (nodeJson.contains("name") && nodeJson["name"].is_string()) node.name = nodeJson["name"].get<std::string>();
            if (!nodeJson.contains("nodetype") || !nodeJson["nodetype"].is_string()) {
                error = "node " + std::to_string(n) + " has no nodetype";
                return false;
            }
            node.category = nodeJson["nodetype"].get<std::string>();
            if (!readType(nodeJson, node.type, error)) {
                error = "node \"" + node.name + "\": " + error;
                return false;
            }
            if (nodeJson.contains("xpos") && nodeJson["xpos"].is_number() && nodeJson.contains("ypos") &&
                nodeJson["ypos"].is_number()) {
                node.position = std::array<float, 2>{nodeJson["xpos"].get<float>(), nodeJson["ypos"].get<float>()};
            }
            if (nodeJson.contains("inputs")) {
                node.inputs.reserve(count(nodeJson["inputs"]));
                const bool ok = forEachPort(nodeJson["inputs"], [&](const std::string& key, const Json& port) {
                    node.inputs.emplace_back();
                    return readInput(key, port, node.inputs.back(), pending, error);
                });
                if (!ok) {
                    error = "node \"" + node.name + "\": " + error;
                    return false;
                }
            }
            if (nodeJson.contains("outputs")) {
                const bool ok = forEachPort(nodeJson["outputs"], [&](const std::string& key, const Json& port) {
                    node.outputs.emplace_back();
                    return readOutput(key, port, node.outputs.back(), error);
                });
                if (!ok) {
                    error = "node \"" + node.name + "\": " + error;
                    return false;
                }
            }
        }
    }
    if (json.contains("outputs")) {
        graph.outputs.reserve(count(json["outputs"]));
        const bool ok = forEachPort(json["outputs"], [&](const std::string& key, const Json& port) {
            graph.outputs.emplace_back();
            if (!readOutput(key, port, graph.outputs.back(), error)) return false;
            if (port.contains("output") && port["output"].is_number_integer()) {
                pendingOutputs.emplace_back(&graph.outputs.back(), port["output"].get<int>());
            }
            return true;
        });
        if (!ok) return false;
    }
    // Index connections to names.
    const auto nodeOutputName = [&](int node, int index, std::string& out) {
        if (node < 0 || size_t(node) >= graph.nodes.size()) return true;  // reported by compile()
        const auto& outputs = graph.nodes[size_t(node)].outputs;
        if (index < 0 || size_t(index) >= outputs.size()) {
            error = "a connection names output " + std::to_string(index) + " of node " + std::to_string(node) +
                    ", which has " + std::to_string(outputs.size());
            return false;
        }
        out = outputs[size_t(index)].name;
        return true;
    };
    for (const auto& p : pending) {
        if (p.graphInputIndex >= 0) {
            if (size_t(p.graphInputIndex) >= graph.inputs.size()) {
                error = "a connection names graph input " + std::to_string(p.graphInputIndex) + ", which doesn't exist";
                return false;
            }
            p.input->graphInput = graph.inputs[size_t(p.graphInputIndex)].name;
        }
        if (p.nodeOutputIndex >= 0 && !nodeOutputName(p.input->node, p.nodeOutputIndex, p.input->output)) return false;
    }
    for (const auto& [output, index] : pendingOutputs) {
        if (!nodeOutputName(output->node, index, output->output)) return false;
    }
    return true;
}

Json writeInput(const Input& input) {
    Json port = Json::object();
    port["nodetype"] = "input";
    port["type"] = toString(input.type);
    if (input.value) port["value"] = writeValue(*input.value);
    if (input.node >= 0) {
        port["node"] = input.node;
        if (!input.output.empty()) port["output"] = input.output;
    }
    if (!input.graphInput.empty()) port["input"] = input.graphInput;
    if (input.texture >= 0) port["texture"] = input.texture;
    if (!input.uiName.empty()) port["uiname"] = input.uiName;
    if (!input.uiFolder.empty()) port["uifolder"] = input.uiFolder;
    if (!input.doc.empty()) port["doc"] = input.doc;
    if (input.uiMin) port["uimin"] = *input.uiMin;
    if (input.uiMax) port["uimax"] = *input.uiMax;
    if (input.uiSoftMin) port["uisoftmin"] = *input.uiSoftMin;
    if (input.uiSoftMax) port["uisoftmax"] = *input.uiSoftMax;
    if (input.uiStep) port["uistep"] = *input.uiStep;
    return port;
}

Json writeOutput(const Output& output) {
    Json port = Json::object();
    port["nodetype"] = "output";
    port["type"] = toString(output.type);
    if (output.node >= 0) port["node"] = output.node;
    if (!output.output.empty()) port["output"] = output.output;
    return port;
}

Json writeGraphJson(const Graph& graph) {
    Json json = Json::object();
    if (!graph.name.empty()) json["name"] = graph.name;
    json["nodetype"] = "nodegraph";
    json["type"] = toString(graph.type);
    Json inputs = Json::object();
    for (const auto& input : graph.inputs) inputs[input.name] = writeInput(input);
    json["inputs"] = inputs;
    Json outputs = Json::object();
    for (const auto& output : graph.outputs) outputs[output.name] = writeOutput(output);
    json["outputs"] = outputs;
    Json nodes = Json::array();
    for (const auto& node : graph.nodes) {
        Json nodeJson = Json::object();
        if (!node.name.empty()) nodeJson["name"] = node.name;
        nodeJson["nodetype"] = node.category;
        nodeJson["type"] = toString(node.type);
        Json nodeInputs = Json::object();
        for (const auto& input : node.inputs) nodeInputs[input.name] = writeInput(input);
        nodeJson["inputs"] = nodeInputs;
        Json nodeOutputs = Json::object();
        for (const auto& output : node.outputs) nodeOutputs[output.name] = writeOutput(output);
        nodeJson["outputs"] = nodeOutputs;
        if (node.position) {
            nodeJson["xpos"] = (*node.position)[0];
            nodeJson["ypos"] = (*node.position)[1];
        }
        nodes.push_back(nodeJson);
    }
    json["nodes"] = nodes;
    return json;
}

}  // namespace

ReadResult readProcedurals(std::string_view text) {
    ReadResult result;
    Json json = Json::parse(text.begin(), text.end(), nullptr, false);
    if (json.is_discarded()) {
        result.error = "invalid JSON";
        return result;
    }
    if (!json.is_object()) {
        result.error = "the JSON isn't an object";
        return result;
    }
    // A glTF document, the extension object, or one graph.
    const Json* procedurals = nullptr;
    if (json.contains("extensions") && json["extensions"].is_object() &&
        json["extensions"].contains("KHR_texture_procedurals")) {
        const auto& extension = json["extensions"]["KHR_texture_procedurals"];
        if (extension.is_object() && extension.contains("procedurals")) procedurals = &extension["procedurals"];
    } else if (json.contains("procedurals")) {
        procedurals = &json["procedurals"];
    }
    if (procedurals) {
        if (!procedurals->is_array()) {
            result.error = "\"procedurals\" isn't an array";
            return result;
        }
        result.graphs.resize(procedurals->size());
        for (size_t i = 0; i < procedurals->size(); ++i) {
            if (!readGraph((*procedurals)[i], result.graphs[i], result.error)) {
                result.graphs.clear();
                return result;
            }
        }
        return result;
    }
    if (json.contains("nodes")) {
        result.graphs.resize(1);
        if (!readGraph(json, result.graphs[0], result.error)) result.graphs.clear();
        return result;
    }
    result.error = "no procedural graphs found";
    return result;
}

std::string writeGraph(const Graph& graph, int indent) {
    return writeGraphJson(graph).dump(indent);
}

std::string writeProcedurals(const std::vector<Graph>& graphs, int indent) {
    Json array = Json::array();
    for (const auto& graph : graphs) array.push_back(writeGraphJson(graph));
    Json json = Json::object();
    json["procedurals"] = array;
    return json.dump(indent);
}

}  // namespace systems::leal::campello_procedural
