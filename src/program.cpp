#include <campello_procedural/bake.hpp>
#include <campello_procedural/nodes.hpp>

#include "noise.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <thread>
#include <unordered_map>

namespace systems::leal::campello_procedural {

namespace {

using Vec4 = std::array<float, 4>;
using VT = ValueType;

enum class Op : uint8_t {
    copy, convert, add, subtract, multiply, divide, modulo, power, safepower, min, max,
    absval, sign, floor, ceil, round, sqrt, exp, ln, sin, cos, tan, asin, acos,
    invert, atan2, clamp, smoothstep, remap, mix, magnitude, distance, dotproduct, normalize, crossproduct,
    rotate2d, place2d, ifgreater, ifgreatereq, ifequal, combine, separate, extract, swizzle,
    luminance, saturate, contrast, hsvtorgb, rgbtohsv,
    checkerboard, ramplr, ramptb, splitlr, splittb, ramp4,
    noise2d, noise3d, fractal2d, fractal3d, cellnoise2d, cellnoise3d, worleynoise2d, worleynoise3d,
    heighttonormal, image, tiledimage, texcoord, position,
};

const std::unordered_map<std::string, Op>& opTable() {
    static const std::unordered_map<std::string, Op> table = {
        {"constant", Op::copy}, {"dot", Op::copy}, {"convert", Op::convert}, {"add", Op::add},
        {"subtract", Op::subtract}, {"multiply", Op::multiply}, {"divide", Op::divide}, {"modulo", Op::modulo},
        {"power", Op::power}, {"safepower", Op::safepower}, {"min", Op::min}, {"max", Op::max},
        {"absval", Op::absval}, {"sign", Op::sign}, {"floor", Op::floor}, {"ceil", Op::ceil}, {"round", Op::round},
        {"sqrt", Op::sqrt}, {"exp", Op::exp}, {"ln", Op::ln}, {"sin", Op::sin}, {"cos", Op::cos}, {"tan", Op::tan},
        {"asin", Op::asin}, {"acos", Op::acos}, {"invert", Op::invert}, {"atan2", Op::atan2}, {"clamp", Op::clamp},
        {"smoothstep", Op::smoothstep}, {"remap", Op::remap}, {"mix", Op::mix}, {"magnitude", Op::magnitude},
        {"distance", Op::distance}, {"dotproduct", Op::dotproduct}, {"normalize", Op::normalize},
        {"crossproduct", Op::crossproduct}, {"rotate2d", Op::rotate2d}, {"place2d", Op::place2d},
        {"ifgreater", Op::ifgreater}, {"ifgreatereq", Op::ifgreatereq}, {"ifequal", Op::ifequal},
        {"combine2", Op::combine}, {"combine3", Op::combine}, {"combine4", Op::combine},
        {"separate2", Op::separate}, {"separate3", Op::separate}, {"separate4", Op::separate},
        {"extract", Op::extract}, {"swizzle", Op::swizzle}, {"luminance", Op::luminance},
        {"saturate", Op::saturate}, {"contrast", Op::contrast}, {"hsvtorgb", Op::hsvtorgb},
        {"rgbtohsv", Op::rgbtohsv}, {"checkerboard", Op::checkerboard}, {"ramplr", Op::ramplr},
        {"ramptb", Op::ramptb}, {"splitlr", Op::splitlr}, {"splittb", Op::splittb}, {"ramp4", Op::ramp4},
        {"noise2d", Op::noise2d}, {"noise3d", Op::noise3d}, {"fractal2d", Op::fractal2d},
        {"fractal3d", Op::fractal3d}, {"cellnoise2d", Op::cellnoise2d}, {"cellnoise3d", Op::cellnoise3d},
        {"worleynoise2d", Op::worleynoise2d}, {"worleynoise3d", Op::worleynoise3d},
        {"heighttonormal", Op::heighttonormal}, {"image", Op::image}, {"tiledimage", Op::tiledimage},
        {"texcoord", Op::texcoord}, {"position", Op::position},
    };
    return table;
}

// Older names for the same nodes.
std::string canonicalCategory(const std::string& category) {
    if (category == "length") return "magnitude";
    return category;
}

struct Operand {
    uint32_t reg = 0;
    uint8_t comps = 1;
};

enum class Address : uint8_t { periodic, clamp, mirror, constant };

struct Instr {
    Op op = Op::copy;
    uint32_t out = 0;
    uint8_t outComps = 1;
    std::vector<Operand> in;
    std::array<int8_t, 4> swizzle{0, 1, 2, 3};  // swizzle: source channel, or -1 / -2 for 0 / 1
    int image = -1;                             // index into Impl::images
    Address uAddress = Address::periodic, vAddress = Address::periodic;
    bool nearest = false;
    std::vector<uint32_t> cone;  // heighttonormal: the instructions its height depends on
};

// Registers 0 and 1: the texel's texture coordinates and position.
constexpr uint32_t kTexcoordReg = 0;
constexpr uint32_t kPositionReg = 1;

}  // namespace

struct Program::Impl {
    Graph graph;
    uint32_t registerCount = 2;
    std::vector<std::pair<uint32_t, Vec4>> constants;
    struct GraphInput {
        std::string name;
        uint32_t reg = 0;
        VT type = VT::none;
    };
    std::vector<GraphInput> graphInputs;
    std::vector<Instr> code;
    struct Out {
        std::string name;
        uint32_t reg = 0;
        uint8_t comps = 1;
    };
    std::vector<Out> outputs;
    std::vector<ImageRef> images;
};

Program::Program(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Program::~Program() = default;
const Graph& Program::graph() const { return impl_->graph; }

namespace {

Vec4 toVec4(const Value& value) { return {value.data[0], value.data[1], value.data[2], value.data[3]}; }

Address parseAddress(const std::string& mode) {
    if (mode == "clamp") return Address::clamp;
    if (mode == "mirror") return Address::mirror;
    if (mode == "constant") return Address::constant;
    return Address::periodic;
}

class Compiler {
public:
    Compiler(const Graph& graph, Program::Impl& impl) : graph_(graph), impl_(impl) {}

    bool run(std::string& error) {
        for (const auto& input : graph_.inputs) {
            if (input.name.empty()) return fail(error, "a graph input has no name");
            Program::Impl::GraphInput gi;
            gi.name = input.name;
            gi.type = input.type;
            gi.reg = impl_.registerCount++;
            impl_.constants.emplace_back(gi.reg, input.value ? toVec4(*input.value) : Vec4{});
            impl_.graphInputs.push_back(gi);
        }
        nodeState_.assign(graph_.nodes.size(), 0);
        nodeOutputs_.resize(graph_.nodes.size());
        if (graph_.outputs.empty()) return fail(error, "the graph has no outputs");
        for (const auto& output : graph_.outputs) {
            Operand operand;
            if (!resolve(output.node, output.output, operand, error)) {
                error = "output \"" + output.name + "\": " + error;
                return false;
            }
            impl_.outputs.push_back({output.name, operand.reg, operand.comps});
        }
        return true;
    }

private:
    static bool fail(std::string& error, std::string message) {
        error = std::move(message);
        return false;
    }

    // The register of a node's output, compiling the node first.
    bool resolve(int node, const std::string& outputName, Operand& out, std::string& error) {
        if (node < 0 || size_t(node) >= graph_.nodes.size()) {
            return fail(error, "connection to node " + std::to_string(node) + ", which doesn't exist");
        }
        if (!compileNode(size_t(node), error)) return false;
        const auto& outputs = nodeOutputs_[size_t(node)];
        if (outputName.empty()) {
            out = outputs.front().second;
            return true;
        }
        for (const auto& [name, operand] : outputs) {
            if (name == outputName) {
                out = operand;
                return true;
            }
        }
        return fail(error, "node \"" + graph_.nodes[size_t(node)].name + "\" has no output \"" + outputName + "\"");
    }

    uint32_t constant(const Vec4& value) {
        const uint32_t reg = impl_.registerCount++;
        impl_.constants.emplace_back(reg, value);
        return reg;
    }

    // The instructions a register depends on (for heighttonormal).
    void collectCone(uint32_t reg, std::vector<uint32_t>& cone) {
        const auto it = producer_.find(reg);
        if (it == producer_.end()) return;
        const uint32_t index = it->second;
        if (std::find(cone.begin(), cone.end(), index) != cone.end()) return;
        for (const auto& operand : impl_.code[index].in) collectCone(operand.reg, cone);
        cone.push_back(index);
    }

    bool compileNode(size_t n, std::string& error) {
        if (nodeState_[n] == 2) return true;
        const auto& node = graph_.nodes[n];
        const std::string label = "node \"" + (node.name.empty() ? std::to_string(n) : node.name) + "\"";
        if (nodeState_[n] == 1) return fail(error, label + " is part of a cycle");
        nodeState_[n] = 1;

        const std::string category = canonicalCategory(node.category);
        const auto opIt = opTable().find(category);
        if (opIt == opTable().end()) return fail(error, label + ": unsupported node \"" + node.category + "\"");
        // separateN nodes are multioutput: their definition is per type separated.
        VT defType = node.type;
        if (opIt->second == Op::separate) {
            const Input* in = node.findInput("in");
            defType = in ? in->type : VT::none;
            if (in && in->node >= 0 && size_t(in->node) < graph_.nodes.size() && defType == VT::none) {
                defType = graph_.nodes[size_t(in->node)].type;
            }
        }
        NodeDef def;
        if (!findNodeDef(category, defType, def)) {
            return fail(error, label + ": \"" + node.category + "\" doesn't produce " + toString(defType));
        }
        for (const auto& input : node.inputs) {
            const bool known = std::any_of(def.inputs.begin(), def.inputs.end(),
                                           [&](const NodeInputDef& d) { return d.name == input.name; });
            if (!known) return fail(error, label + " has no input \"" + input.name + "\"");
        }

        Instr instr;
        instr.op = opIt->second;
        for (const auto& inputDef : def.inputs) {
            const Input* input = node.findInput(inputDef.name);
            const std::string where = label + " input \"" + inputDef.name + "\"";
            // Text inputs: values only.
            if (inputDef.type == VT::string || inputDef.type == VT::filename) {
                std::string text = inputDef.defaultValue.text;
                int texture = -1;
                if (input && input->value) text = input->value->text;
                if (input && input->texture >= 0) texture = input->texture;
                if (input && !input->graphInput.empty()) {
                    const Input* graphInput = graph_.findInput(input->graphInput);
                    if (!graphInput) return fail(error, where + ": no graph input \"" + input->graphInput + "\"");
                    if (graphInput->value) text = graphInput->value->text;
                    if (graphInput->texture >= 0) texture = graphInput->texture;
                }
                if (input && input->node >= 0) return fail(error, where + " can't be connected to a node");
                applyText(instr, inputDef.name, text, texture);
                continue;
            }
            Operand operand;
            if (!input || !(input->value || input->isConnected())) {
                switch (inputDef.source) {
                    case DefaultSource::texcoord: operand = {kTexcoordReg, 2}; break;
                    case DefaultSource::position: operand = {kPositionReg, 3}; break;
                    case DefaultSource::value:
                        operand = {constant(toVec4(inputDef.defaultValue)),
                                   uint8_t(std::max(1, componentCount(inputDef.defaultValue.type)))};
                        break;
                }
            } else if (input->value) {
                const int comps = componentCount(input->type);
                if (comps < 1 || comps > 4) return fail(error, where + ": unsupported type " + toString(input->type));
                operand = {constant(toVec4(*input->value)), uint8_t(comps)};
            } else if (input->node >= 0) {
                if (!resolve(input->node, input->output, operand, error)) {
                    error = where + ": " + error;
                    return false;
                }
            } else if (!input->graphInput.empty()) {
                const auto it = std::find_if(impl_.graphInputs.begin(), impl_.graphInputs.end(),
                                             [&](const auto& gi) { return gi.name == input->graphInput; });
                if (it == impl_.graphInputs.end()) {
                    return fail(error, where + ": no graph input \"" + input->graphInput + "\"");
                }
                const int comps = componentCount(it->type);
                if (comps < 1 || comps > 4) return fail(error, where + ": unsupported type " + toString(it->type));
                operand = {it->reg, uint8_t(comps)};
            } else {
                return fail(error, where + ": a texture can only feed an image's file");
            }
            const int expected = componentCount(inputDef.type);
            if (inputDef.type != VT::none && operand.comps != expected && operand.comps != 1) {
                return fail(error, where + " expects " + std::to_string(expected) + " components, got " +
                                       std::to_string(operand.comps));
            }
            instr.in.push_back(operand);
        }

        // Outputs: one register each.
        auto& outputs = nodeOutputs_[n];
        instr.out = impl_.registerCount;
        for (const auto& outputDef : def.outputs) {
            outputs.emplace_back(outputDef.name, Operand{impl_.registerCount++, uint8_t(std::max(1, componentCount(outputDef.type)))});
        }
        instr.outComps = outputs.front().second.comps;
        if (instr.op == Op::separate) instr.outComps = uint8_t(def.outputs.size());
        if (instr.op == Op::heighttonormal) collectCone(instr.in[0].reg, instr.cone);
        const uint32_t index = uint32_t(impl_.code.size());
        for (const auto& [_, operand] : outputs) producer_[operand.reg] = index;
        impl_.code.push_back(std::move(instr));
        nodeState_[n] = 2;
        return true;
    }

    void applyText(Instr& instr, const std::string& name, const std::string& text, int texture) {
        if (name == "channels") {
            for (size_t i = 0; i < 4; ++i) {
                const char c = i < text.size() ? text[i] : 'x';
                switch (c) {
                    case 'x': case 'r': instr.swizzle[i] = 0; break;
                    case 'y': case 'g': instr.swizzle[i] = 1; break;
                    case 'z': case 'b': instr.swizzle[i] = 2; break;
                    case 'w': case 'a': instr.swizzle[i] = 3; break;
                    case '1': instr.swizzle[i] = -2; break;
                    default: instr.swizzle[i] = -1; break;
                }
            }
        } else if (name == "file") {
            ImageRef ref;
            ref.filename = text;
            ref.texture = texture;
            instr.image = int(impl_.images.size());
            impl_.images.push_back(ref);
        } else if (name == "uaddressmode") {
            instr.uAddress = parseAddress(text);
        } else if (name == "vaddressmode") {
            instr.vAddress = parseAddress(text);
        } else if (name == "filtertype") {
            instr.nearest = text == "closest";
        }
    }

    const Graph& graph_;
    Program::Impl& impl_;
    std::vector<int> nodeState_;  // 0 new, 1 compiling, 2 done
    std::vector<std::vector<std::pair<std::string, Operand>>> nodeOutputs_;
    std::unordered_map<uint32_t, uint32_t> producer_;  // register -> instruction
};

// ---------------------------------------------------------------------------
// Evaluation

struct Context {
    const Program::Impl* impl = nullptr;
    const std::vector<std::shared_ptr<const Image>>* images = nullptr;
    float du = 0.0f, dv = 0.0f;  // one texel, for heighttonormal
};

inline Vec4 load(const Vec4* regs, const Operand& o) {
    const Vec4& v = regs[o.reg];
    if (o.comps == 1) return {v[0], v[0], v[0], v[0]};
    return v;
}

template <typename F>
inline Vec4 each(const Vec4& a, F f) {
    return {f(a[0]), f(a[1]), f(a[2]), f(a[3])};
}
template <typename F>
inline Vec4 each(const Vec4& a, const Vec4& b, F f) {
    return {f(a[0], b[0]), f(a[1], b[1]), f(a[2], b[2]), f(a[3], b[3])};
}

inline float mixf(float bg, float fg, float m) { return bg * (1.0f - m) + fg * m; }
inline Vec4 mix(const Vec4& bg, const Vec4& fg, const Vec4& m) {
    return {mixf(bg[0], fg[0], m[0]), mixf(bg[1], fg[1], m[1]), mixf(bg[2], fg[2], m[2]), mixf(bg[3], fg[3], m[3])};
}
inline Vec4 mix(const Vec4& bg, const Vec4& fg, float m) { return mix(bg, fg, Vec4{m, m, m, m}); }
inline float clamp01(float v) { return std::min(1.0f, std::max(0.0f, v)); }

float dot(const Vec4& a, const Vec4& b, int n) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i) s += a[size_t(i)] * b[size_t(i)];
    return s;
}

Vec4 hsvToRgb(const Vec4& c) {
    const float h = c[0] - std::floor(c[0]), s = c[1], v = c[2];
    const float h6 = h * 6.0f;
    const int i = int(std::floor(h6)) % 6;
    const float f = h6 - std::floor(h6);
    const float p = v * (1.0f - s), q = v * (1.0f - s * f), t = v * (1.0f - s * (1.0f - f));
    switch (i) {
        case 0: return {v, t, p, c[3]};
        case 1: return {q, v, p, c[3]};
        case 2: return {p, v, t, c[3]};
        case 3: return {p, q, v, c[3]};
        case 4: return {t, p, v, c[3]};
        default: return {v, p, q, c[3]};
    }
}

Vec4 rgbToHsv(const Vec4& c) {
    const float r = c[0], g = c[1], b = c[2];
    const float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
    const float delta = mx - mn;
    float h = 0.0f;
    if (delta > 0.0f) {
        if (mx == r) h = (g - b) / delta;
        else if (mx == g) h = 2.0f + (b - r) / delta;
        else h = 4.0f + (r - g) / delta;
        h /= 6.0f;
        if (h < 0.0f) h += 1.0f;
    }
    const float s = mx > 0.0f ? delta / mx : 0.0f;
    return {h, s, mx, c[3]};
}

float addressCoord(float t, Address mode, bool& outside) {
    switch (mode) {
        case Address::clamp: return clamp01(t);
        case Address::mirror: {
            const float m = t - 2.0f * std::floor(t * 0.5f);
            return m > 1.0f ? 2.0f - m : m;
        }
        case Address::constant:
            if (t < 0.0f || t > 1.0f) outside = true;
            return t;
        default: return t - std::floor(t);
    }
}

Vec4 sampleImage(const Image& image, float u, float v, const Instr& instr, const Vec4& fallback) {
    if (image.width == 0 || image.height == 0) return fallback;
    bool outside = false;
    // MaterialX coordinates: v up; image rows top to bottom.
    const float s = addressCoord(u, instr.uAddress, outside);
    const float t = 1.0f - addressCoord(v, instr.vAddress, outside);
    if (outside) return fallback;
    const auto wrap = [](int i, int n, Address mode) {
        if (mode == Address::periodic) return ((i % n) + n) % n;
        return std::min(n - 1, std::max(0, i));
    };
    const float x = s * float(image.width) - 0.5f;
    const float y = t * float(image.height) - 0.5f;
    if (instr.nearest) {
        const int ix = wrap(int(std::floor(x + 0.5f)), int(image.width), instr.uAddress);
        const int iy = wrap(int(std::floor(y + 0.5f)), int(image.height), instr.vAddress);
        const float* p = image.texel(uint32_t(ix), uint32_t(iy));
        return {p[0], p[1], p[2], p[3]};
    }
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - float(x0), fy = y - float(y0);
    Vec4 out{};
    for (int j = 0; j < 2; ++j) {
        for (int i = 0; i < 2; ++i) {
            const float w = (i ? fx : 1.0f - fx) * (j ? fy : 1.0f - fy);
            const float* p = image.texel(uint32_t(wrap(x0 + i, int(image.width), instr.uAddress)),
                                         uint32_t(wrap(y0 + j, int(image.height), instr.vAddress)));
            for (int c = 0; c < 4; ++c) out[size_t(c)] += w * p[c];
        }
    }
    return out;
}

template <typename P>
Vec4 perlinOut(P p, int comps) {
    if (comps == 1) {
        const float n = noise::perlin(p);
        return {n, n, n, n};
    }
    const auto v = noise::perlinVec3(p);
    float w = 0.0f;
    if (comps == 4) {
        P q = p;
        q[0] += 19.0f;
        q[1] += 73.0f;
        if constexpr (std::tuple_size_v<P> == 3) q[2] += 37.0f;
        w = noise::perlin(q);
    }
    return {v[0], v[1], v[2], w};
}

void exec(const Instr& in, Vec4* regs, const Context& ctx);

void runCone(const std::vector<uint32_t>& cone, Vec4* regs, const Context& ctx) {
    for (const uint32_t index : cone) exec(ctx.impl->code[index], regs, ctx);
}

void exec(const Instr& in, Vec4* regs, const Context& ctx) {
    const auto a = [&](size_t i) { return load(regs, in.in[i]); };
    Vec4& out = regs[in.out];
    const int n = in.outComps;
    switch (in.op) {
        case Op::copy: out = a(0); break;
        case Op::convert: {
            const Vec4 v = a(0);
            const int from = in.in[0].comps;
            out = v;
            if (from == 1) out = {v[0], v[0], v[0], v[0]};
            else {
                for (int i = from; i < 4; ++i) out[size_t(i)] = 0.0f;
                if (n == 4 && from < 4) out[3] = 1.0f;
            }
            break;
        }
        case Op::add: out = each(a(0), a(1), [](float x, float y) { return x + y; }); break;
        case Op::subtract: out = each(a(0), a(1), [](float x, float y) { return x - y; }); break;
        case Op::multiply: out = each(a(0), a(1), [](float x, float y) { return x * y; }); break;
        case Op::divide: out = each(a(0), a(1), [](float x, float y) { return x / y; }); break;
        case Op::modulo: out = each(a(0), a(1), [](float x, float y) { return x - y * std::floor(x / y); }); break;
        case Op::power: out = each(a(0), a(1), [](float x, float y) { return std::pow(x, y); }); break;
        case Op::safepower:
            out = each(a(0), a(1), [](float x, float y) { return (x < 0.0f ? -1.0f : 1.0f) * std::pow(std::fabs(x), y); });
            break;
        case Op::min: out = each(a(0), a(1), [](float x, float y) { return std::min(x, y); }); break;
        case Op::max: out = each(a(0), a(1), [](float x, float y) { return std::max(x, y); }); break;
        case Op::absval: out = each(a(0), [](float x) { return std::fabs(x); }); break;
        case Op::sign: out = each(a(0), [](float x) { return float((x > 0.0f) - (x < 0.0f)); }); break;
        case Op::floor: out = each(a(0), [](float x) { return std::floor(x); }); break;
        case Op::ceil: out = each(a(0), [](float x) { return std::ceil(x); }); break;
        case Op::round: out = each(a(0), [](float x) { return std::round(x); }); break;
        case Op::sqrt: out = each(a(0), [](float x) { return std::sqrt(x); }); break;
        case Op::exp: out = each(a(0), [](float x) { return std::exp(x); }); break;
        case Op::ln: out = each(a(0), [](float x) { return std::log(x); }); break;
        case Op::sin: out = each(a(0), [](float x) { return std::sin(x); }); break;
        case Op::cos: out = each(a(0), [](float x) { return std::cos(x); }); break;
        case Op::tan: out = each(a(0), [](float x) { return std::tan(x); }); break;
        case Op::asin: out = each(a(0), [](float x) { return std::asin(x); }); break;
        case Op::acos: out = each(a(0), [](float x) { return std::acos(x); }); break;
        case Op::invert: out = each(a(0), a(1), [](float x, float amount) { return amount - x; }); break;
        case Op::atan2: out = each(a(0), a(1), [](float y, float x) { return std::atan2(y, x); }); break;
        case Op::clamp: {
            const Vec4 v = a(0), lo = a(1), hi = a(2);
            for (int i = 0; i < 4; ++i) out[size_t(i)] = std::min(std::max(v[size_t(i)], lo[size_t(i)]), hi[size_t(i)]);
            break;
        }
        case Op::smoothstep: {
            const Vec4 v = a(0), lo = a(1), hi = a(2);
            for (size_t i = 0; i < 4; ++i) {
                if (v[i] >= hi[i]) out[i] = 1.0f;
                else if (v[i] <= lo[i]) out[i] = 0.0f;
                else {
                    const float t = (v[i] - lo[i]) / (hi[i] - lo[i]);
                    out[i] = t * t * (3.0f - 2.0f * t);
                }
            }
            break;
        }
        case Op::remap: {
            const Vec4 v = a(0), il = a(1), ih = a(2), ol = a(3), oh = a(4);
            for (size_t i = 0; i < 4; ++i) out[i] = ol[i] + (v[i] - il[i]) * (oh[i] - ol[i]) / (ih[i] - il[i]);
            break;
        }
        case Op::mix: out = mix(a(1), a(0), a(2)); break;
        case Op::magnitude: {
            const float m = std::sqrt(dot(a(0), a(0), in.in[0].comps));
            out = {m, m, m, m};
            break;
        }
        case Op::distance: {
            const Vec4 d = each(a(0), a(1), [](float x, float y) { return x - y; });
            const float m = std::sqrt(dot(d, d, std::max(in.in[0].comps, in.in[1].comps)));
            out = {m, m, m, m};
            break;
        }
        case Op::dotproduct: {
            const float d = dot(a(0), a(1), std::max(in.in[0].comps, in.in[1].comps));
            out = {d, d, d, d};
            break;
        }
        case Op::normalize: {
            const Vec4 v = a(0);
            const float len = std::sqrt(dot(v, v, n));
            out = len > 0.0f ? each(v, [len](float x) { return x / len; }) : v;
            break;
        }
        case Op::crossproduct: {
            const Vec4 p = a(0), q = a(1);
            out = {p[1] * q[2] - p[2] * q[1], p[2] * q[0] - p[0] * q[2], p[0] * q[1] - p[1] * q[0], 0.0f};
            break;
        }
        case Op::rotate2d: {
            const Vec4 v = a(0);
            const float r = a(1)[0] * 3.14159265358979f / 180.0f;
            const float s = std::sin(r), c = std::cos(r);
            out = {c * v[0] + s * v[1], -s * v[0] + c * v[1], 0.0f, 0.0f};
            break;
        }
        case Op::place2d: {
            const Vec4 uv = a(0), pivot = a(1), scale = a(2), offset = a(4);
            const float r = a(3)[0] * 3.14159265358979f / 180.0f;
            const float s = std::sin(r), c = std::cos(r);
            const auto rot = [&](float x, float y) { return std::array<float, 2>{c * x + s * y, -s * x + c * y}; };
            float x = uv[0] - pivot[0], y = uv[1] - pivot[1];
            if (int(a(5)[0]) == 0) {  // SRT
                const auto p = rot(x / scale[0], y / scale[1]);
                out = {p[0] - offset[0] + pivot[0], p[1] - offset[1] + pivot[1], 0.0f, 0.0f};
            } else {  // TRS
                const auto p = rot(x - offset[0], y - offset[1]);
                out = {p[0] / scale[0] + pivot[0], p[1] / scale[1] + pivot[1], 0.0f, 0.0f};
            }
            break;
        }
        case Op::ifgreater: out = a(0)[0] > a(1)[0] ? a(2) : a(3); break;
        case Op::ifgreatereq: out = a(0)[0] >= a(1)[0] ? a(2) : a(3); break;
        case Op::ifequal: out = a(0)[0] == a(1)[0] ? a(2) : a(3); break;
        case Op::combine:
            out = {0.0f, 0.0f, 0.0f, 0.0f};
            for (size_t i = 0; i < in.in.size(); ++i) out[i] = a(i)[0];
            break;
        case Op::separate: {
            const Vec4 v = a(0);
            for (int i = 0; i < n; ++i) {
                const float c = v[size_t(i)];
                regs[in.out + uint32_t(i)] = {c, c, c, c};
            }
            break;
        }
        case Op::extract: {
            const int index = std::min(3, std::max(0, int(a(1)[0])));
            const float c = a(0)[size_t(index)];
            out = {c, c, c, c};
            break;
        }
        case Op::swizzle: {
            const Vec4 v = a(0);
            Vec4 r{};
            for (size_t i = 0; i < 4; ++i) {
                const int8_t s = in.swizzle[i];
                r[i] = s >= 0 ? v[size_t(s)] : (s == -2 ? 1.0f : 0.0f);
            }
            out = r;
            break;
        }
        case Op::luminance: {
            const Vec4 c = a(0);
            const float l = dot(c, a(1), 3);
            out = {l, l, l, c[3]};
            break;
        }
        case Op::saturate: {
            const Vec4 c = a(0);
            const float amount = a(1)[0];
            const float l = dot(c, a(2), 3);
            out = {l + (c[0] - l) * amount, l + (c[1] - l) * amount, l + (c[2] - l) * amount, c[3]};
            break;
        }
        case Op::contrast: {
            const float amount = a(1)[0], pivot = a(2)[0];
            out = each(a(0), [&](float x) { return (x - pivot) * amount + pivot; });
            break;
        }
        case Op::hsvtorgb: out = hsvToRgb(a(0)); break;
        case Op::rgbtohsv: out = rgbToHsv(a(0)); break;
        case Op::checkerboard: {
            const Vec4 c1 = a(0), c2 = a(1), tiling = a(2), offset = a(3), uv = a(4);
            const float x = std::floor(uv[0] * tiling[0] - offset[0]);
            const float y = std::floor(uv[1] * tiling[1] - offset[1]);
            const float s = x + y;
            const float m = s - 2.0f * std::floor(s * 0.5f);
            out = mix(c2, c1, m);
            out[3] = 0.0f;
            break;
        }
        case Op::ramplr: out = mix(a(0), a(1), clamp01(a(2)[0])); break;
        case Op::ramptb: out = mix(a(1), a(0), clamp01(a(2)[1])); break;
        case Op::splitlr: out = a(3)[0] >= a(2)[0] ? a(1) : a(0); break;
        case Op::splittb: out = a(3)[1] >= a(2)[0] ? a(0) : a(1); break;
        case Op::ramp4: {
            const Vec4 uv = a(4);
            const float s = clamp01(uv[0]), t = clamp01(uv[1]);
            const Vec4 top = mix(a(0), a(1), s), bottom = mix(a(2), a(3), s);
            out = mix(bottom, top, t);
            break;
        }
        case Op::noise2d:
        case Op::noise3d: {
            const Vec4 amplitude = a(0);
            const float pivot = a(1)[0];
            const Vec4 p = a(2);
            const Vec4 v = in.op == Op::noise2d ? perlinOut(noise::Vec2{p[0], p[1]}, n)
                                                : perlinOut(noise::Vec3{p[0], p[1], p[2]}, n);
            for (size_t i = 0; i < 4; ++i) out[i] = v[i] * amplitude[i] + pivot;
            break;
        }
        case Op::fractal2d:
        case Op::fractal3d: {
            const Vec4 amplitude = a(0);
            const int octaves = std::min(32, std::max(0, int(a(1)[0])));
            const float lacunarity = a(2)[0], diminish = a(3)[0];
            Vec4 p = a(4);
            Vec4 sum{};
            float amp = 1.0f;
            for (int o = 0; o < octaves; ++o) {
                const Vec4 v = in.op == Op::fractal2d ? perlinOut(noise::Vec2{p[0], p[1]}, n)
                                                      : perlinOut(noise::Vec3{p[0], p[1], p[2]}, n);
                for (size_t i = 0; i < 4; ++i) sum[i] += amp * v[i];
                amp *= diminish;
                for (size_t i = 0; i < 3; ++i) p[i] *= lacunarity;
            }
            for (size_t i = 0; i < 4; ++i) out[i] = sum[i] * amplitude[i];
            break;
        }
        case Op::cellnoise2d:
        case Op::cellnoise3d: {
            const Vec4 p = a(0);
            if (n == 1) {
                const float c = in.op == Op::cellnoise2d ? noise::cell(noise::Vec2{p[0], p[1]})
                                                         : noise::cell(noise::Vec3{p[0], p[1], p[2]});
                out = {c, c, c, c};
            } else {
                const auto c = in.op == Op::cellnoise2d ? noise::cellVec3(noise::Vec2{p[0], p[1]})
                                                        : noise::cellVec3(noise::Vec3{p[0], p[1], p[2]});
                out = {c[0], c[1], c[2], 0.0f};
            }
            break;
        }
        case Op::worleynoise2d:
        case Op::worleynoise3d: {
            const Vec4 p = a(0);
            const float jitter = a(1)[0];
            const float d = in.op == Op::worleynoise2d ? noise::worley(noise::Vec2{p[0], p[1]}, jitter)
                                                       : noise::worley(noise::Vec3{p[0], p[1], p[2]}, jitter);
            out = {d, d, d, d};
            break;
        }
        case Op::heighttonormal: {
            // Central differences of the height in texture coordinates (one
            // texel apart), re-evaluating the nodes the height depends on.
            const Vec4 uv0 = regs[kTexcoordReg], pos0 = regs[kPositionReg];
            const auto heightAt = [&](float du, float dv) {
                regs[kTexcoordReg] = {uv0[0] + du, uv0[1] + dv, 0.0f, 0.0f};
                regs[kPositionReg] = {pos0[0] + du, pos0[1] + dv, pos0[2], 0.0f};
                runCone(in.cone, regs, ctx);
                return regs[in.in[0].reg][0];
            };
            const float scale = a(1)[0];
            const float hx = (heightAt(ctx.du, 0.0f) - heightAt(-ctx.du, 0.0f)) / (2.0f * ctx.du);
            const float hy = (heightAt(0.0f, ctx.dv) - heightAt(0.0f, -ctx.dv)) / (2.0f * ctx.dv);
            regs[kTexcoordReg] = uv0;
            regs[kPositionReg] = pos0;
            runCone(in.cone, regs, ctx);  // the centre's values again, for later readers
            float nx = -hx * scale, ny = -hy * scale, nz = 1.0f;
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            nx /= len;
            ny /= len;
            nz /= len;
            regs[in.out] = {nx * 0.5f + 0.5f, ny * 0.5f + 0.5f, nz * 0.5f + 0.5f, 0.0f};
            break;
        }
        case Op::image:
        case Op::tiledimage: {
            // image: default, texcoord. tiledimage: default, texcoord, uvtiling, uvoffset.
            const Vec4 fallback = a(0);
            Vec4 uv = a(1);
            if (in.op == Op::tiledimage) {
                const Vec4 tiling = a(2), offset = a(3);
                uv = {uv[0] * tiling[0] + offset[0], uv[1] * tiling[1] + offset[1], 0.0f, 0.0f};
            }
            const auto& image = (*ctx.images)[size_t(in.image)];
            out = image ? sampleImage(*image, uv[0], uv[1], in, fallback) : fallback;
            break;
        }
        case Op::texcoord: out = regs[kTexcoordReg]; break;
        case Op::position: out = regs[kPositionReg]; break;
    }
}

uint8_t outputComps(const Program::Impl& impl, const std::string& name, uint32_t& reg, bool& found) {
    for (const auto& out : impl.outputs) {
        if (out.name == name) {
            reg = out.reg;
            found = true;
            return out.comps;
        }
    }
    found = false;
    return 0;
}

}  // namespace

std::shared_ptr<const Program> Program::compile(const Graph& graph, std::string* error) {
    auto impl = std::make_unique<Impl>();
    impl->graph = graph;
    std::string message;
    Compiler compiler(impl->graph, *impl);
    if (!compiler.run(message)) {
        if (error) *error = message;
        return nullptr;
    }
    return std::shared_ptr<const Program>(new Program(std::move(impl)));
}

const Image* BakeResult::find(std::string_view name) const {
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return &images[i];
    }
    return nullptr;
}

BakeResult bake(const Program& program, const BakeOptions& options) {
    BakeResult result;
    const auto& impl = program.impl();
    if (options.width == 0 || options.height == 0) {
        result.error = "the bake size is zero";
        return result;
    }
    // The outputs asked for.
    struct Target {
        uint32_t reg;
        uint8_t comps;
    };
    std::vector<Target> targets;
    const auto addTarget = [&](const std::string& name) {
        uint32_t reg = 0;
        bool found = false;
        const uint8_t comps = outputComps(impl, name, reg, found);
        if (!found) {
            result.error = "the graph has no output \"" + name + "\"";
            return false;
        }
        result.names.push_back(name);
        targets.push_back({reg, comps});
        return true;
    };
    if (options.outputs.empty()) {
        for (const auto& out : impl.outputs) addTarget(out.name);
    } else {
        for (const auto& name : options.outputs) {
            if (!addTarget(name)) return result;
        }
    }
    // Registers: constants and graph inputs (with the values asked for).
    std::vector<Vec4> initial(impl.registerCount, Vec4{});
    for (const auto& [reg, value] : impl.constants) initial[reg] = value;
    for (const auto& [name, value] : options.inputs) {
        const auto it = std::find_if(impl.graphInputs.begin(), impl.graphInputs.end(),
                                     [&](const auto& gi) { return gi.name == name; });
        if (it == impl.graphInputs.end()) {
            result.error = "the graph has no input \"" + name + "\"";
            result.names.clear();
            return result;
        }
        initial[it->reg] = toVec4(value);
    }
    // Images.
    std::vector<std::shared_ptr<const Image>> images(impl.images.size());
    if (options.images) {
        for (size_t i = 0; i < impl.images.size(); ++i) images[i] = options.images(impl.images[i]);
    }

    const uint32_t w = options.width, h = options.height;
    result.images.resize(targets.size());
    for (auto& image : result.images) {
        image.width = w;
        image.height = h;
        image.rgba.assign(size_t(w) * h * 4, 0.0f);
    }
    Context ctx;
    ctx.impl = &impl;
    ctx.images = &images;
    ctx.du = 1.0f / float(w);
    ctx.dv = 1.0f / float(h);

    std::atomic<uint32_t> nextRow{0};
    const auto worker = [&] {
        std::vector<Vec4> regs = initial;
        for (uint32_t y = nextRow++; y < h; y = nextRow++) {
            // MaterialX coordinates: v up, so row 0 (the top) is v = 1.
            const float v = 1.0f - (float(y) + 0.5f) / float(h);
            for (uint32_t x = 0; x < w; ++x) {
                const float u = (float(x) + 0.5f) / float(w);
                regs[kTexcoordReg] = {u, v, 0.0f, 0.0f};
                regs[kPositionReg] = {u, v, 0.0f, 0.0f};
                for (const auto& instr : impl.code) exec(instr, regs.data(), ctx);
                for (size_t t = 0; t < targets.size(); ++t) {
                    const Vec4& value = regs[targets[t].reg];
                    float* texel = result.images[t].texel(x, y);
                    switch (targets[t].comps) {
                        case 1: texel[0] = texel[1] = texel[2] = value[0]; texel[3] = 1.0f; break;
                        case 2: texel[0] = value[0]; texel[1] = value[1]; texel[2] = 0.0f; texel[3] = 1.0f; break;
                        case 3: texel[0] = value[0]; texel[1] = value[1]; texel[2] = value[2]; texel[3] = 1.0f; break;
                        default: for (int c = 0; c < 4; ++c) texel[c] = value[size_t(c)]; break;
                    }
                }
            }
        }
    };
    uint32_t threads = options.threads ? options.threads : std::max(1u, std::thread::hardware_concurrency());
    threads = std::min(threads, h);
    std::vector<std::thread> pool;
    for (uint32_t t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& thread : pool) thread.join();
    return result;
}

BakeResult bake(const Graph& graph, const BakeOptions& options) {
    std::string error;
    const auto program = Program::compile(graph, &error);
    if (!program) {
        BakeResult result;
        result.error = error;
        return result;
    }
    return bake(*program, options);
}

namespace {

uint8_t toByte(float v) { return uint8_t(std::lround(clamp01(v) * 255.0f)); }

float srgbEncode(float linear) {
    const float v = clamp01(linear);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

}  // namespace

std::vector<uint8_t> toRGBA8(const Image& image, Encoding encoding) {
    std::vector<uint8_t> out(size_t(image.width) * image.height * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        for (size_t c = 0; c < 3; ++c) {
            const float v = image.rgba[i + c];
            out[i + c] = toByte(encoding == Encoding::srgb ? srgbEncode(v) : v);
        }
        out[i + 3] = toByte(image.rgba[i + 3]);
    }
    return out;
}

std::vector<Image> mipChain(const Image& image, MipFilter filter) {
    std::vector<Image> chain;
    const Image* source = &image;
    while (source->width > 1 || source->height > 1) {
        Image level;
        level.width = std::max(1u, source->width / 2);
        level.height = std::max(1u, source->height / 2);
        level.rgba.resize(size_t(level.width) * level.height * 4);
        for (uint32_t y = 0; y < level.height; ++y) {
            for (uint32_t x = 0; x < level.width; ++x) {
                float sum[4] = {0, 0, 0, 0};
                for (uint32_t j = 0; j < 2; ++j) {
                    for (uint32_t i = 0; i < 2; ++i) {
                        const float* p = source->texel(std::min(source->width - 1, x * 2 + i),
                                                       std::min(source->height - 1, y * 2 + j));
                        for (int c = 0; c < 4; ++c) sum[c] += p[c] * 0.25f;
                    }
                }
                float* q = level.texel(x, y);
                if (filter == MipFilter::normal) {
                    float n[3] = {sum[0] * 2.0f - 1.0f, sum[1] * 2.0f - 1.0f, sum[2] * 2.0f - 1.0f};
                    const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    if (len > 0.0f) {
                        for (float& c : n) c /= len;
                    } else {
                        n[0] = n[1] = 0.0f;
                        n[2] = 1.0f;
                    }
                    for (int c = 0; c < 3; ++c) q[c] = n[c] * 0.5f + 0.5f;
                    q[3] = sum[3];
                } else {
                    for (int c = 0; c < 4; ++c) q[c] = sum[c];
                }
            }
        }
        chain.push_back(std::move(level));
        source = &chain.back();
    }
    return chain;
}

}  // namespace systems::leal::campello_procedural
