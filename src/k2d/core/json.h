#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace k2d {

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue, std::less<>>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum class Type {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
    };

    using Storage = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;

    Storage value;

    JsonValue() : value(nullptr) {}

    static JsonValue makeNull() { return JsonValue(); }
    static JsonValue makeBool(bool v);
    static JsonValue makeNumber(double v);
    static JsonValue makeString(std::string v);
    static JsonValue makeArray(JsonArray v);
    static JsonValue makeObject(JsonObject v);

    Type type() const;

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool *asBool();
    double *asNumber();
    std::string *asString();
    JsonArray *asArray();
    JsonObject *asObject();

    const bool *asBool() const;
    const double *asNumber() const;
    const std::string *asString() const;
    const JsonArray *asArray() const;
    const JsonObject *asObject() const;

    JsonValue *get(std::string_view key);
    const JsonValue *get(std::string_view key) const;

    // Convenience helpers.
    std::optional<std::string> getString(std::string_view key) const;
    std::optional<double> getNumber(std::string_view key) const;
    std::optional<bool> getBool(std::string_view key) const;
};

struct JsonParseError {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::string message;
};

// Parse a complete JSON document.
std::optional<JsonValue> ParseJson(std::string_view text, JsonParseError *outError);

// Serialize JSON value with pretty indentation.
std::string StringifyJson(const JsonValue &value, int indent_spaces = 2);

}  // namespace k2d
