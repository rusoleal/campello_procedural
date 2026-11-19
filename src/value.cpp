#include <campello_procedural/graph.hpp>
#include <campello_procedural/value.hpp>

#include <utility>

namespace systems::leal::campello_procedural {

namespace {

struct TypeName {
    ValueType type;
    const char* name;
};

constexpr TypeName kTypeNames[] = {
    {ValueType::boolean, "boolean"},   {ValueType::integer, "integer"},   {ValueType::float1, "float"},
    {ValueType::vector2, "vector2"},   {ValueType::vector3, "vector3"},   {ValueType::vector4, "vector4"},
    {ValueType::color3, "color3"},     {ValueType::color4, "color4"},     {ValueType::matrix33, "matrix33"},
    {ValueType::matrix44, "matrix44"}, {ValueType::string, "string"},     {ValueType::filename, "filename"},
    {ValueType::multioutput, "multioutput"},
};

}  // namespace

const char* toString(ValueType type) {
    for (const auto& entry : kTypeNames) {
        if (entry.type == type) return entry.name;
    }
    return "";
}

std::optional<ValueType> valueTypeFromString(std::string_view name) {
    for (const auto& entry : kTypeNames) {
        if (name == entry.name) return entry.type;
    }
    return std::nullopt;
}

int componentCount(ValueType type) {
    switch (type) {
        case ValueType::boolean:
        case ValueType::integer:
        case ValueType::float1: return 1;
        case ValueType::vector2: return 2;
        case ValueType::vector3:
        case ValueType::color3: return 3;
        case ValueType::vector4:
        case ValueType::color4: return 4;
        case ValueType::matrix33: return 9;
        case ValueType::matrix44: return 16;
        default: return 0;
    }
}

bool isNumeric(ValueType type) {
    const int n = componentCount(type);
    return n >= 1 && n <= 4;
}

Value Value::makeBoolean(bool v) {
    Value value;
    value.type = ValueType::boolean;
    value.data[0] = v ? 1.0f : 0.0f;
    return value;
}

Value Value::makeInteger(int v) {
    Value value;
    value.type = ValueType::integer;
    value.data[0] = float(v);
    return value;
}

Value Value::makeFloat(float v) {
    Value value;
    value.type = ValueType::float1;
    value.data[0] = v;
    return value;
}

Value Value::makeVector2(float x, float y) {
    Value value;
    value.type = ValueType::vector2;
    value.data[0] = x;
    value.data[1] = y;
    return value;
}

Value Value::makeVector3(float x, float y, float z) {
    Value value;
    value.type = ValueType::vector3;
    value.data[0] = x;
    value.data[1] = y;
    value.data[2] = z;
    return value;
}

Value Value::makeVector4(float x, float y, float z, float w) {
    Value value;
    value.type = ValueType::vector4;
    value.data[0] = x;
    value.data[1] = y;
    value.data[2] = z;
    value.data[3] = w;
    return value;
}

Value Value::makeColor3(float r, float g, float b) {
    Value value = makeVector3(r, g, b);
    value.type = ValueType::color3;
    return value;
}

Value Value::makeColor4(float r, float g, float b, float a) {
    Value value = makeVector4(r, g, b, a);
    value.type = ValueType::color4;
    return value;
}

Value Value::makeString(std::string text) {
    Value value;
    value.type = ValueType::string;
    value.text = std::move(text);
    return value;
}

Value Value::makeFilename(std::string text) {
    Value value;
    value.type = ValueType::filename;
    value.text = std::move(text);
    return value;
}

bool Value::operator==(const Value& other) const {
    return type == other.type && data == other.data && text == other.text;
}

const Input* Node::findInput(std::string_view inputName) const {
    for (const auto& input : inputs) {
        if (input.name == inputName) return &input;
    }
    return nullptr;
}

Input* Node::findInput(std::string_view inputName) {
    for (auto& input : inputs) {
        if (input.name == inputName) return &input;
    }
    return nullptr;
}

const Input* Graph::findInput(std::string_view inputName) const {
    for (const auto& input : inputs) {
        if (input.name == inputName) return &input;
    }
    return nullptr;
}

Input* Graph::findInput(std::string_view inputName) {
    for (auto& input : inputs) {
        if (input.name == inputName) return &input;
    }
    return nullptr;
}

int Graph::findOutput(std::string_view outputName) const {
    for (size_t i = 0; i < outputs.size(); ++i) {
        if (outputs[i].name == outputName) return int(i);
    }
    return -1;
}

}  // namespace systems::leal::campello_procedural
