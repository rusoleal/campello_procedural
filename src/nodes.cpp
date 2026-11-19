#include <campello_procedural/nodes.hpp>

#include <algorithm>
#include <initializer_list>
#include <map>

namespace systems::leal::campello_procedural {

namespace {

using VT = ValueType;

// The numeric types most nodes are defined for.
constexpr VT kFloats[] = {VT::float1, VT::color3, VT::color4, VT::vector2, VT::vector3, VT::vector4};
constexpr VT kColors[] = {VT::color3, VT::color4};
constexpr VT kVectors[] = {VT::vector2, VT::vector3, VT::vector4};

// A value of `type` with every component `v` (a MaterialX scalar default
// applied to a vector or colour input).
Value splat(VT type, float v) {
    Value value;
    value.type = type;
    for (int i = 0; i < std::max(1, componentCount(type)); ++i) value.data[size_t(i)] = v;
    return value;
}

NodeInputDef in(const char* name, VT type, float v) {
    return NodeInputDef{name, type, splat(type, v), DefaultSource::value};
}
NodeInputDef in(const char* name, Value value) {
    const VT type = value.type;
    return NodeInputDef{name, type, std::move(value), DefaultSource::value};
}
NodeInputDef texcoordIn(const char* name = "texcoord") {
    return NodeInputDef{name, VT::vector2, splat(VT::vector2, 0.0f), DefaultSource::texcoord};
}
NodeInputDef positionIn(const char* name = "position") {
    return NodeInputDef{name, VT::vector3, splat(VT::vector3, 0.0f), DefaultSource::position};
}
// An input of any numeric type (its type comes from what's connected).
NodeInputDef anyIn(const char* name, float v = 0.0f) {
    return NodeInputDef{name, VT::none, splat(VT::float1, v), DefaultSource::value};
}
NodeInputDef stringIn(const char* name, const char* v) {
    return NodeInputDef{name, VT::string, Value::makeString(v), DefaultSource::value};
}

using InputsFor = std::vector<NodeInputDef> (*)(VT type);

struct Registry {
    // category -> type -> definition
    std::map<std::string, std::map<VT, NodeDef>, std::less<>> defs;

    void add(const char* category, const char* group, VT type, std::vector<NodeInputDef> inputs,
             std::vector<NodeOutputDef> outputs = {}) {
        NodeDef def;
        def.category = category;
        def.type = type;
        def.group = group;
        def.inputs = std::move(inputs);
        def.outputs = outputs.empty() ? std::vector<NodeOutputDef>{{"out", type}} : std::move(outputs);
        defs[category][type] = std::move(def);
    }
    template <size_t N, typename F>
    void addEach(const char* category, const char* group, const VT (&types)[N], F inputs) {
        for (VT type : types) add(category, group, type, inputs(type));
    }
};

Registry build() {
    Registry r;
    // Values and geometry.
    r.addEach("constant", "procedural", kFloats, [](VT t) { return std::vector{in("value", t, 0.0f)}; });
    r.addEach("dot", "organization", kFloats, [](VT t) { return std::vector{in("in", t, 0.0f)}; });
    r.add("texcoord", "geometric", VT::vector2, {in("index", VT::integer, 0.0f)});
    r.add("texcoord", "geometric", VT::vector3, {in("index", VT::integer, 0.0f)});
    r.add("position", "geometric", VT::vector3, {stringIn("space", "object")});

    // Math.
    for (const char* binary : {"add", "subtract"}) {
        r.addEach(binary, "math", kFloats, [](VT t) { return std::vector{in("in1", t, 0.0f), in("in2", t, 0.0f)}; });
    }
    for (const char* binary : {"multiply", "divide", "modulo", "power", "safepower"}) {
        r.addEach(binary, "math", kFloats, [](VT t) { return std::vector{in("in1", t, 0.0f), in("in2", t, 1.0f)}; });
    }
    for (const char* binary : {"min", "max"}) {
        r.addEach(binary, "math", kFloats, [](VT t) { return std::vector{in("in1", t, 0.0f), in("in2", t, 0.0f)}; });
    }
    for (const char* unary : {"absval", "sign", "floor", "ceil", "round", "sqrt", "exp", "ln", "sin", "cos", "tan",
                              "asin", "acos"}) {
        r.addEach(unary, "math", kFloats, [](VT t) { return std::vector{in("in", t, 0.0f)}; });
    }
    r.addEach("invert", "math", kFloats, [](VT t) { return std::vector{in("in", t, 0.0f), in("amount", t, 1.0f)}; });
    r.addEach("atan2", "math", kFloats, [](VT t) { return std::vector{in("iny", t, 0.0f), in("inx", t, 1.0f)}; });
    r.addEach("clamp", "math", kFloats,
              [](VT t) { return std::vector{in("in", t, 0.0f), in("low", t, 0.0f), in("high", t, 1.0f)}; });
    r.addEach("smoothstep", "math", kFloats,
              [](VT t) { return std::vector{in("in", t, 0.0f), in("low", t, 0.0f), in("high", t, 1.0f)}; });
    r.addEach("remap", "math", kFloats, [](VT t) {
        return std::vector{in("in", t, 0.0f), in("inlow", t, 0.0f), in("inhigh", t, 1.0f), in("outlow", t, 0.0f),
                           in("outhigh", t, 1.0f)};
    });
    r.addEach("mix", "compositing", kFloats,
              [](VT t) { return std::vector{in("fg", t, 0.0f), in("bg", t, 0.0f), in("mix", t, 0.0f)}; });
    r.add("magnitude", "math", VT::float1, {anyIn("in")});
    r.add("distance", "math", VT::float1, {anyIn("in1"), anyIn("in2")});
    r.add("dotproduct", "math", VT::float1, {anyIn("in1"), anyIn("in2")});
    r.addEach("normalize", "math", kVectors, [](VT t) { return std::vector{in("in", t, 0.0f)}; });
    r.add("crossproduct", "math", VT::vector3, {in("in1", VT::vector3, 0.0f), in("in2", VT::vector3, 0.0f)});
    r.add("rotate2d", "math", VT::vector2, {in("in", VT::vector2, 0.0f), in("amount", VT::float1, 0.0f)});
    r.add("place2d", "math", VT::vector2,
          {texcoordIn(), in("pivot", VT::vector2, 0.0f), in("scale", VT::vector2, 1.0f), in("rotate", VT::float1, 0.0f),
           in("offset", VT::vector2, 0.0f), in("operationorder", VT::integer, 0.0f)});

    // Conditionals.
    r.addEach("ifgreater", "conditional", kFloats, [](VT t) {
        return std::vector{in("value1", VT::float1, 1.0f), in("value2", VT::float1, 0.0f), in("in1", t, 0.0f),
                           in("in2", t, 0.0f)};
    });
    r.addEach("ifgreatereq", "conditional", kFloats, [](VT t) {
        return std::vector{in("value1", VT::float1, 1.0f), in("value2", VT::float1, 0.0f), in("in1", t, 0.0f),
                           in("in2", t, 0.0f)};
    });
    r.addEach("ifequal", "conditional", kFloats, [](VT t) {
        return std::vector{in("value1", VT::float1, 0.0f), in("value2", VT::float1, 0.0f), in("in1", t, 0.0f),
                           in("in2", t, 0.0f)};
    });

    // Channels.
    r.add("combine2", "channel", VT::vector2, {in("in1", VT::float1, 0.0f), in("in2", VT::float1, 0.0f)});
    for (VT t : {VT::vector3, VT::color3}) {
        r.add("combine3", "channel", t,
              {in("in1", VT::float1, 0.0f), in("in2", VT::float1, 0.0f), in("in3", VT::float1, 0.0f)});
    }
    for (VT t : {VT::vector4, VT::color4}) {
        r.add("combine4", "channel", t,
              {in("in1", VT::float1, 0.0f), in("in2", VT::float1, 0.0f), in("in3", VT::float1, 0.0f),
               in("in4", VT::float1, 0.0f)});
    }
    // separateN: defined per type separated (the node itself is multioutput).
    r.add("separate2", "channel", VT::vector2, {in("in", VT::vector2, 0.0f)},
          {{"outx", VT::float1}, {"outy", VT::float1}});
    r.add("separate3", "channel", VT::vector3, {in("in", VT::vector3, 0.0f)},
          {{"outx", VT::float1}, {"outy", VT::float1}, {"outz", VT::float1}});
    r.add("separate3", "channel", VT::color3, {in("in", VT::color3, 0.0f)},
          {{"outr", VT::float1}, {"outg", VT::float1}, {"outb", VT::float1}});
    r.add("separate4", "channel", VT::vector4, {in("in", VT::vector4, 0.0f)},
          {{"outx", VT::float1}, {"outy", VT::float1}, {"outz", VT::float1}, {"outw", VT::float1}});
    r.add("separate4", "channel", VT::color4, {in("in", VT::color4, 0.0f)},
          {{"outr", VT::float1}, {"outg", VT::float1}, {"outb", VT::float1}, {"outa", VT::float1}});
    r.add("extract", "channel", VT::float1, {anyIn("in"), in("index", VT::integer, 0.0f)});
    r.addEach("convert", "channel", kFloats, [](VT) { return std::vector{anyIn("in")}; });
    // MaterialX 1.38's swizzle (removed in 1.39), still found in graphs.
    r.addEach("swizzle", "channel", kFloats, [](VT) { return std::vector{anyIn("in"), stringIn("channels", "")}; });

    // Colour adjustment.
    const Value luma = Value::makeColor3(0.2722287f, 0.6740818f, 0.0536895f);
    r.addEach("luminance", "adjustment", kColors,
              [&](VT t) { return std::vector{in("in", t, 0.0f), in("lumacoeffs", luma)}; });
    r.addEach("saturate", "adjustment", kColors, [&](VT t) {
        return std::vector{in("in", t, 0.0f), in("amount", VT::float1, 1.0f), in("lumacoeffs", luma)};
    });
    r.addEach("contrast", "adjustment", kFloats, [](VT t) {
        return std::vector{in("in", t, 0.0f), in("amount", VT::float1, 1.0f), in("pivot", VT::float1, 0.5f)};
    });
    r.addEach("hsvtorgb", "adjustment", kColors, [](VT t) { return std::vector{in("in", t, 0.0f)}; });
    r.addEach("rgbtohsv", "adjustment", kColors, [](VT t) { return std::vector{in("in", t, 0.0f)}; });

    // Procedurals.
    r.add("checkerboard", "texture2d", VT::color3,
          {in("color1", VT::color3, 1.0f), in("color2", VT::color3, 0.0f), in("uvtiling", VT::vector2, 8.0f),
           in("uvoffset", VT::vector2, 0.0f), texcoordIn()});
    r.addEach("ramplr", "texture2d", kFloats,
              [](VT t) { return std::vector{in("valuel", t, 0.0f), in("valuer", t, 0.0f), texcoordIn()}; });
    r.addEach("ramptb", "texture2d", kFloats,
              [](VT t) { return std::vector{in("valuet", t, 0.0f), in("valueb", t, 0.0f), texcoordIn()}; });
    r.addEach("splitlr", "texture2d", kFloats, [](VT t) {
        return std::vector{in("valuel", t, 0.0f), in("valuer", t, 0.0f), in("center", VT::float1, 0.5f), texcoordIn()};
    });
    r.addEach("splittb", "texture2d", kFloats, [](VT t) {
        return std::vector{in("valuet", t, 0.0f), in("valueb", t, 0.0f), in("center", VT::float1, 0.5f), texcoordIn()};
    });
    r.addEach("ramp4", "texture2d", kFloats, [](VT t) {
        return std::vector{in("valuetl", t, 0.0f), in("valuetr", t, 0.0f), in("valuebl", t, 0.0f),
                           in("valuebr", t, 0.0f), texcoordIn()};
    });
    r.addEach("noise2d", "procedural2d", kFloats, [](VT t) {
        return std::vector{in("amplitude", t, 1.0f), in("pivot", VT::float1, 0.0f), texcoordIn()};
    });
    r.addEach("noise3d", "procedural3d", kFloats, [](VT t) {
        return std::vector{in("amplitude", t, 1.0f), in("pivot", VT::float1, 0.0f), positionIn()};
    });
    r.addEach("fractal2d", "procedural2d", kFloats, [](VT t) {
        return std::vector{in("amplitude", t, 1.0f), in("octaves", VT::integer, 3.0f),
                           in("lacunarity", VT::float1, 2.0f), in("diminish", VT::float1, 0.5f), texcoordIn()};
    });
    r.addEach("fractal3d", "procedural3d", kFloats, [](VT t) {
        return std::vector{in("amplitude", t, 1.0f), in("octaves", VT::integer, 3.0f),
                           in("lacunarity", VT::float1, 2.0f), in("diminish", VT::float1, 0.5f), positionIn()};
    });
    for (VT t : {VT::float1, VT::vector3}) {
        r.add("cellnoise2d", "procedural2d", t, {texcoordIn()});
        r.add("cellnoise3d", "procedural3d", t, {positionIn()});
    }
    r.add("worleynoise2d", "procedural2d", VT::float1, {texcoordIn(), in("jitter", VT::float1, 1.0f)});
    r.add("worleynoise3d", "procedural3d", VT::float1, {positionIn(), in("jitter", VT::float1, 1.0f)});

    // Normals.
    r.add("heighttonormal", "nprlib", VT::vector3,
          {in("in", VT::float1, 0.0f), in("scale", VT::float1, 1.0f), texcoordIn()});

    // Images.
    r.addEach("image", "texture2d", kFloats, [](VT t) {
        return std::vector{in("file", Value::makeFilename("")), stringIn("layer", ""), in("default", t, 0.0f),
                           texcoordIn(), stringIn("uaddressmode", "periodic"),
                           stringIn("vaddressmode", "periodic"), stringIn("filtertype", "linear")};
    });
    r.addEach("tiledimage", "texture2d", kFloats, [](VT t) {
        return std::vector{in("file", Value::makeFilename("")), in("default", t, 0.0f), texcoordIn(),
                           in("uvtiling", VT::vector2, 1.0f), in("uvoffset", VT::vector2, 0.0f),
                           stringIn("filtertype", "linear")};
    });
    return r;
}

const Registry& registry() {
    static const Registry r = build();
    return r;
}

}  // namespace

std::vector<std::string> nodeCategories() {
    std::vector<std::string> categories;
    for (const auto& [category, _] : registry().defs) categories.push_back(category);
    return categories;
}

bool findNodeDef(std::string_view category, ValueType type, NodeDef& out) {
    const auto& defs = registry().defs;
    const auto it = defs.find(category);
    if (it == defs.end()) return false;
    const auto typeIt = it->second.find(type);
    if (typeIt == it->second.end()) return false;
    out = typeIt->second;
    return true;
}

std::vector<ValueType> nodeTypes(std::string_view category) {
    std::vector<ValueType> types;
    const auto& defs = registry().defs;
    const auto it = defs.find(category);
    if (it == defs.end()) return types;
    for (const auto& [type, _] : it->second) types.push_back(type);
    return types;
}

}  // namespace systems::leal::campello_procedural
