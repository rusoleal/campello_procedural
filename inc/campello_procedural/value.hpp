#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace systems::leal::campello_procedural {

/// The data types of KHR_texture_procedurals (MaterialX's type names).
enum class ValueType : uint8_t {
    none,
    boolean,
    integer,
    float1,  ///< "float"
    vector2,
    vector3,
    vector4,
    color3,
    color4,
    matrix33,
    matrix44,
    string,
    filename,
    multioutput,  ///< a node or graph with several outputs
};

/// The MaterialX name of a type ("float", "color3", ...).
const char* toString(ValueType type);
/// The type with a MaterialX name; nullopt for unknown names.
std::optional<ValueType> valueTypeFromString(std::string_view name);
/// Numeric components: 1 for boolean, integer and float, 2-4 for vectors and
/// colours, 9 and 16 for matrices, 0 for strings, filenames and multioutput.
int componentCount(ValueType type);
/// True for the types the evaluator computes with (boolean, integer, float,
/// vectors and colours).
bool isNumeric(ValueType type);

/// A typed constant: numeric components in `data` (unused ones 0), or text
/// for string and filename values.
struct Value {
    ValueType type = ValueType::none;
    std::array<float, 16> data{};
    std::string text;

    static Value makeBoolean(bool v);
    static Value makeInteger(int v);
    static Value makeFloat(float v);
    static Value makeVector2(float x, float y);
    static Value makeVector3(float x, float y, float z);
    static Value makeVector4(float x, float y, float z, float w);
    static Value makeColor3(float r, float g, float b);
    static Value makeColor4(float r, float g, float b, float a);
    static Value makeString(std::string text);
    static Value makeFilename(std::string text);

    bool operator==(const Value& other) const;
};

}  // namespace systems::leal::campello_procedural
