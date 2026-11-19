#include <campello_procedural/materialx.hpp>
#include <campello_procedural/nodes.hpp>

#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace systems::leal::campello_procedural {

namespace {

// ---------------------------------------------------------------------------
// A small XML reader: elements and attributes (what MaterialX uses); text
// content, comments, declarations and processing instructions are skipped.

struct Element {
    std::string tag;
    std::vector<std::pair<std::string, std::string>> attributes;
    std::vector<Element> children;

    const std::string* attribute(std::string_view name) const {
        for (const auto& [key, value] : attributes) {
            if (key == name) return &value;
        }
        return nullptr;
    }
    std::string get(std::string_view name) const {
        const std::string* value = attribute(name);
        return value ? *value : std::string();
    }
};

class XmlReader {
public:
    explicit XmlReader(std::string_view text) : text_(text) {}

    bool read(Element& root, std::string& error) {
        skipMisc();
        if (!element(root)) {
            error = error_.empty() ? "not an XML document" : error_;
            return false;
        }
        return true;
    }

private:
    bool at(std::string_view s) const { return text_.substr(pos_, s.size()) == s; }
    void skipSpace() {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) ++pos_;
    }
    // Skips whitespace, comments, <?...?> and <!...> declarations.
    void skipMisc() {
        for (;;) {
            skipSpace();
            if (at("<!--")) {
                const auto end = text_.find("-->", pos_);
                pos_ = end == std::string_view::npos ? text_.size() : end + 3;
            } else if (at("<?")) {
                const auto end = text_.find("?>", pos_);
                pos_ = end == std::string_view::npos ? text_.size() : end + 2;
            } else if (at("<!")) {
                const auto end = text_.find('>', pos_);
                pos_ = end == std::string_view::npos ? text_.size() : end + 1;
            } else {
                return;
            }
        }
    }
    static bool nameChar(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':' || c == '.';
    }
    std::string name() {
        const size_t start = pos_;
        while (pos_ < text_.size() && nameChar(text_[pos_])) ++pos_;
        return std::string(text_.substr(start, pos_ - start));
    }
    static std::string decode(std::string_view raw) {
        std::string out;
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '&') {
                out += raw[i];
                continue;
            }
            const auto end = raw.find(';', i);
            if (end == std::string_view::npos) {
                out += raw[i];
                continue;
            }
            const auto entity = raw.substr(i + 1, end - i - 1);
            if (entity == "lt") out += '<';
            else if (entity == "gt") out += '>';
            else if (entity == "amp") out += '&';
            else if (entity == "quot") out += '"';
            else if (entity == "apos") out += '\'';
            else if (!entity.empty() && entity[0] == '#') {
                const long code = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')
                                      ? std::strtol(std::string(entity.substr(2)).c_str(), nullptr, 16)
                                      : std::strtol(std::string(entity.substr(1)).c_str(), nullptr, 10);
                if (code < 0x80) out += char(code);  // ASCII only; others are dropped
            } else {
                out += raw.substr(i, end - i + 1);
            }
            i = end;
        }
        return out;
    }
    bool fail(std::string message) {
        error_ = std::move(message) + " (at byte " + std::to_string(pos_) + ")";
        return false;
    }
    bool element(Element& out) {
        if (!at("<")) return fail("expected an element");
        ++pos_;
        out.tag = name();
        if (out.tag.empty()) return fail("an element has no name");
        for (;;) {
            skipSpace();
            if (at("/>")) {
                pos_ += 2;
                return true;
            }
            if (at(">")) {
                ++pos_;
                break;
            }
            std::string key = name();
            if (key.empty()) return fail("bad attribute in <" + out.tag + ">");
            skipSpace();
            if (!at("=")) return fail("attribute " + key + " has no value");
            ++pos_;
            skipSpace();
            if (pos_ >= text_.size() || (text_[pos_] != '"' && text_[pos_] != '\'')) return fail("attribute " + key + " isn't quoted");
            const char quote = text_[pos_++];
            const auto end = text_.find(quote, pos_);
            if (end == std::string_view::npos) return fail("unterminated attribute " + key);
            out.attributes.emplace_back(std::move(key), decode(text_.substr(pos_, end - pos_)));
            pos_ = end + 1;
        }
        // Children until the closing tag; text between them is skipped.
        for (;;) {
            while (pos_ < text_.size() && text_[pos_] != '<') ++pos_;
            if (pos_ >= text_.size()) return fail("<" + out.tag + "> isn't closed");
            if (at("<!--") || at("<?") || at("<!")) {
                skipMisc();
                continue;
            }
            if (at("</")) {
                pos_ += 2;
                const std::string closing = name();
                skipSpace();
                if (closing != out.tag || !at(">")) return fail("</" + closing + "> closes <" + out.tag + ">");
                ++pos_;
                return true;
            }
            out.children.emplace_back();
            if (!element(out.children.back())) return false;
        }
    }

    std::string_view text_;
    size_t pos_ = 0;
    std::string error_;
};

// ---------------------------------------------------------------------------
// MaterialX to graphs.

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

/// A value written as text: "0.5", "0.2, 0.4, 0.6", "true", a file name.
Value parseValue(ValueType type, const std::string& text, bool srgb) {
    Value value;
    value.type = type;
    if (type == ValueType::string || type == ValueType::filename) {
        value.text = text;
        return value;
    }
    if (type == ValueType::boolean) {
        value.data[0] = text == "true" || text == "1" ? 1.0f : 0.0f;
        return value;
    }
    size_t i = 0;
    const char* p = text.c_str();
    while (*p != '\0' && i < value.data.size()) {
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p) {
            ++p;
            continue;
        }
        value.data[i++] = v;
        p = end;
    }
    if (srgb && (type == ValueType::color3 || type == ValueType::color4)) {
        for (size_t c = 0; c < 3; ++c) value.data[c] = srgbToLinear(value.data[c]);
    }
    return value;
}

bool isSrgb(const Element& element) {
    const std::string colorspace = element.get("colorspace");
    return colorspace == "srgb_texture" || colorspace == "srgb_displayp3" || colorspace == "g22_rec709";
}

// Shader inputs and the slots they become.
struct Slot {
    const char* input;
    const char* output;
    ValueType type;
};

const std::vector<Slot>* shaderSlots(const std::string& category) {
    static const std::map<std::string, std::vector<Slot>> table = {
        {"standard_surface",
         {{"base_color", "base_color", ValueType::color3}, {"specular_roughness", "roughness", ValueType::float1},
          {"metalness", "metallic", ValueType::float1}, {"normal", "normal", ValueType::vector3},
          {"emission_color", "emissive", ValueType::color3}}},
        {"gltf_pbr",
         {{"base_color", "base_color", ValueType::color3}, {"roughness", "roughness", ValueType::float1},
          {"metallic", "metallic", ValueType::float1}, {"normal", "normal", ValueType::vector3},
          {"emissive", "emissive", ValueType::color3}}},
        {"open_pbr_surface",
         {{"base_color", "base_color", ValueType::color3}, {"specular_roughness", "roughness", ValueType::float1},
          {"base_metalness", "metallic", ValueType::float1}, {"geometry_normal", "normal", ValueType::vector3},
          {"emission_color", "emissive", ValueType::color3}}},
        {"UsdPreviewSurface",
         {{"diffuseColor", "base_color", ValueType::color3}, {"roughness", "roughness", ValueType::float1},
          {"metallic", "metallic", ValueType::float1}, {"normal", "normal", ValueType::vector3},
          {"emissiveColor", "emissive", ValueType::color3}}},
    };
    const auto it = table.find(category);
    return it == table.end() ? nullptr : &it->second;
}

// Top-level elements that aren't nodes.
bool isStructural(const std::string& tag) {
    static const std::set<std::string> tags = {"nodegraph", "nodedef", "implementation", "typedef", "look", "lookgroup",
                                               "materialassign", "collection", "geominfo", "geompropdef", "backdrop",
                                               "variantset", "propertyset", "unit", "unitdef", "unittypedef", "token",
                                               "input", "output", "surfacematerial", "volumematerial", "material",
                                               "attributedef", "targetdef", "member"};
    return tags.count(tag) != 0;
}

/// One material's graph, built from the document.
class Converter {
public:
    Converter(const Element& document, std::vector<std::string>& warnings) : warnings_(warnings) {
        for (const auto& child : document.children) {
            if (child.tag == "nodegraph") {
                if (!child.attribute("nodedef")) graphs_[child.get("name")] = &child;  // functional graphs aren't
            } else if (!isStructural(child.tag)) {
                topNodes_[child.get("name")] = &child;
            }
        }
    }

    Graph convert(const Element& shader, const std::string& name) {
        graph_ = Graph{};
        nodes_.clear();
        inputs_.clear();
        graph_.name = name;
        const auto* slots = shaderSlots(shader.tag);
        bool hasNormal = false;
        for (const auto& slot : *slots) {
            const Element* input = child(shader, "input", slot.input);
            if (!input) continue;
            auto [node, output] = connection("", *input, std::string(slot.output) == "normal");
            if (node < 0) {
                if (!input->attribute("value")) continue;
                node = constant(slot.type, parseValue(slot.type, input->get("value"), isSrgb(*input)));
                output.clear();
            }
            graph_.outputs.push_back({slot.output, slot.type, node, output});
            hasNormal = hasNormal || std::string(slot.output) == "normal";
        }
        if (!hasNormal) {
            graph_.outputs.push_back({"normal", ValueType::vector3, constant(ValueType::vector3, Value::makeVector3(0.5f, 0.5f, 1.0f)), ""});
        }
        graph_.type = graph_.outputs.size() == 1 ? graph_.outputs[0].type : ValueType::multioutput;
        return std::move(graph_);
    }

private:
    static const Element* child(const Element& parent, std::string_view tag, std::string_view name) {
        for (const auto& c : parent.children) {
            if (c.tag == tag && c.get("name") == name) return &c;
        }
        return nullptr;
    }

    int constant(ValueType type, const Value& value) {
        Node node;
        node.name = "constant" + std::to_string(graph_.nodes.size());
        node.category = "constant";
        node.type = type;
        Input in;
        in.name = "value";
        in.type = type;
        in.value = value;
        node.inputs.push_back(in);
        node.outputs.push_back({"out", type, -1, ""});
        graph_.nodes.push_back(std::move(node));
        return int(graph_.nodes.size()) - 1;
    }

    /// What an <input> (of a shader, a node or a graph output) connects to:
    /// a node index and output, or -1. `unwrapNormalMap`: a normalmap node
    /// stands for its tangent-space input.
    std::pair<int, std::string> connection(const std::string& scope, const Element& input, bool unwrapNormalMap = false) {
        const std::string output = input.get("output");
        if (const auto* graphName = input.attribute("nodegraph")) {
            return graphOutput(*graphName, output, unwrapNormalMap);
        }
        if (const auto* nodeName = input.attribute("nodename")) {
            return node(scope, *nodeName, output, unwrapNormalMap);
        }
        return {-1, ""};
    }

    std::pair<int, std::string> graphOutput(const std::string& graphName, const std::string& outputName, bool unwrapNormalMap) {
        const auto it = graphs_.find(graphName);
        if (it == graphs_.end()) {
            warnings_.push_back("node graph '" + graphName + "' not found (or functional)");
            return {-1, ""};
        }
        const Element* out = nullptr;
        for (const auto& c : it->second->children) {
            if (c.tag == "output" && (outputName.empty() || c.get("name") == outputName)) {
                out = &c;
                break;
            }
        }
        if (!out) {
            warnings_.push_back("node graph '" + graphName + "' has no output '" + outputName + "'");
            return {-1, ""};
        }
        return connection(graphName, *out, unwrapNormalMap);
    }

    std::pair<int, std::string> node(const std::string& scope, const std::string& nodeName, const std::string& output, bool unwrapNormalMap) {
        const Element* element = nullptr;
        if (!scope.empty()) {
            for (const auto& c : graphs_.at(scope)->children) {
                if (c.tag != "input" && c.tag != "output" && c.get("name") == nodeName) {
                    element = &c;
                    break;
                }
            }
        } else if (const auto it = topNodes_.find(nodeName); it != topNodes_.end()) {
            element = it->second;
        }
        if (!element) {
            warnings_.push_back("node '" + nodeName + "' not found");
            return {-1, ""};
        }
        if (unwrapNormalMap && element->tag == "normalmap") {
            // Its tangent-space input stands for it (normals are tangent space here).
            const Element* in = child(*element, "input", "in");
            if (!in) return {constant(ValueType::vector3, Value::makeVector3(0.5f, 0.5f, 1.0f)), ""};
            auto inner = connection(scope, *in);
            if (inner.first < 0 && in->attribute("value")) {
                inner = {constant(ValueType::vector3, parseValue(ValueType::vector3, in->get("value"), false)), ""};
            }
            return inner;
        }
        const std::string key = scope + "/" + nodeName;
        if (const auto it = nodes_.find(key); it != nodes_.end()) return {it->second, output};
        if (visiting_.count(key)) {
            warnings_.push_back("node '" + nodeName + "' is part of a cycle");
            return {-1, ""};
        }
        visiting_.insert(key);
        const ValueType type = valueTypeFromString(element->get("type")).value_or(ValueType::none);
        int index;
        NodeDef def;
        const bool known = findNodeDef(element->tag, type, def) || type == ValueType::multioutput ||
                           (element->tag == "length");
        if (!known) {
            warnings_.push_back("node '" + nodeName + "' (" + element->tag + ", " + element->get("type") +
                                ") isn't supported: replaced by a constant");
            index = constant(type == ValueType::none ? ValueType::float1 : type, Value{type, {}, ""});
        } else {
            Node n;
            n.name = (scope.empty() ? std::string() : scope + "_") + nodeName;
            n.category = element->tag;
            n.type = type;
            for (const auto& c : element->children) {
                if (c.tag != "input") continue;
                Input in;
                in.name = c.get("name");
                in.type = valueTypeFromString(c.get("type")).value_or(ValueType::none);
                if (const auto* interface = c.attribute("interfacename"); interface && !scope.empty()) {
                    in.graphInput = graphInput(scope, *interface);
                    if (in.graphInput.empty()) continue;  // its default
                } else if (c.attribute("nodename") || c.attribute("nodegraph")) {
                    const auto [from, fromOutput] = connection(scope, c);
                    if (from < 0) continue;
                    in.node = from;
                    in.output = fromOutput;
                } else if (c.attribute("value")) {
                    in.value = parseValue(in.type, c.get("value"), isSrgb(c));
                    if ((element->tag == "image" || element->tag == "tiledimage") && in.name == "file") {
                        warnings_.push_back("image '" + in.value->text + "': files aren't read, so it gives its default");
                    }
                } else {
                    continue;
                }
                n.inputs.push_back(std::move(in));
            }
            n.outputs.push_back({"out", type, -1, ""});
            graph_.nodes.push_back(std::move(n));
            index = int(graph_.nodes.size()) - 1;
        }
        visiting_.erase(key);
        nodes_[key] = index;
        return {index, output};
    }

    /// The graph input a node graph's interface input becomes (named after
    /// it, prefixed with the graph's name when another graph already has
    /// one so named), or "" when it has no value to start from.
    std::string graphInput(const std::string& scope, const std::string& name) {
        const std::string key = scope + "/" + name;
        if (const auto it = inputs_.find(key); it != inputs_.end()) return it->second;
        const Element* source = child(*graphs_.at(scope), "input", name);
        if (!source || !source->attribute("value")) {
            inputs_[key] = "";
            return "";
        }
        std::string ours = name;
        if (graph_.findInput(ours)) ours = scope + "_" + name;
        Input in;
        in.name = ours;
        in.type = valueTypeFromString(source->get("type")).value_or(ValueType::none);
        in.value = parseValue(in.type, source->get("value"), isSrgb(*source));
        in.uiName = source->get("uiname");
        in.uiFolder = source->get("uifolder");
        in.doc = source->get("doc");
        const auto number = [&](const char* attribute, std::optional<float>& out) {
            if (const auto* text = source->attribute(attribute)) out = std::strtof(text->c_str(), nullptr);
        };
        number("uimin", in.uiMin);
        number("uimax", in.uiMax);
        number("uisoftmin", in.uiSoftMin);
        number("uisoftmax", in.uiSoftMax);
        number("uistep", in.uiStep);
        graph_.inputs.push_back(std::move(in));
        inputs_[key] = ours;
        return ours;
    }

    std::vector<std::string>& warnings_;
    std::map<std::string, const Element*> graphs_;
    std::map<std::string, const Element*> topNodes_;
    Graph graph_;
    std::map<std::string, int> nodes_;
    std::map<std::string, std::string> inputs_;
    std::set<std::string> visiting_;
};

}  // namespace

MaterialXResult readMaterialX(std::string_view text) {
    MaterialXResult result;
    Element document;
    std::string error;
    if (!XmlReader(text).read(document, error)) {
        result.error = error;
        return result;
    }
    if (document.tag != "materialx") {
        result.error = "not a MaterialX document";
        return result;
    }
    Converter converter(document, result.warnings);
    const auto shaderNamed = [&](const std::string& name) -> const Element* {
        for (const auto& c : document.children) {
            if (c.get("name") == name) return &c;
        }
        return nullptr;
    };
    // Materials, each with its surface shader.
    std::set<const Element*> used;
    for (const auto& c : document.children) {
        if (c.tag != "surfacematerial" && c.tag != "material") continue;
        const Element* shader = nullptr;
        for (const auto& in : c.children) {
            if (in.tag == "input" && in.get("name") == "surfaceshader") shader = shaderNamed(in.get("nodename"));
        }
        if (!shader) {
            result.warnings.push_back("material '" + c.get("name") + "' has no surface shader");
            continue;
        }
        if (!shaderSlots(shader->tag)) {
            result.warnings.push_back("material '" + c.get("name") + "': shader " + shader->tag + " isn't supported");
            continue;
        }
        used.insert(shader);
        result.graphs.push_back(converter.convert(*shader, c.get("name")));
    }
    // Shaders no material uses (documents without materials).
    if (result.graphs.empty()) {
        for (const auto& c : document.children) {
            if (c.get("type") == "surfaceshader" && shaderSlots(c.tag) && !used.count(&c)) {
                result.graphs.push_back(converter.convert(c, c.get("name")));
            }
        }
    }
    if (result.graphs.empty()) result.error = "no supported surface material in the document";
    return result;
}

}  // namespace systems::leal::campello_procedural
