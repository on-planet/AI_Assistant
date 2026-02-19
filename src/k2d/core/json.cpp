#include "json.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace k2d {

// =========================================================
// JsonValue Implementation
// =========================================================

JsonValue JsonValue::makeBool(bool v) {
    JsonValue j;
    j.value = v;
    return j;
}

JsonValue JsonValue::makeNumber(double v) {
    JsonValue j;
    j.value = v;
    return j;
}

JsonValue JsonValue::makeString(std::string v) {
    JsonValue j;
    j.value = std::move(v);
    return j;
}

JsonValue JsonValue::makeArray(JsonArray v) {
    JsonValue j;
    j.value = std::move(v);
    return j;
}

JsonValue JsonValue::makeObject(JsonObject v) {
    JsonValue j;
    j.value = std::move(v);
    return j;
}

JsonValue::Type JsonValue::type() const {
    if (std::holds_alternative<std::nullptr_t>(value)) return Type::Null;
    if (std::holds_alternative<bool>(value)) return Type::Bool;
    if (std::holds_alternative<double>(value)) return Type::Number;
    if (std::holds_alternative<std::string>(value)) return Type::String;
    if (std::holds_alternative<JsonArray>(value)) return Type::Array;
    if (std::holds_alternative<JsonObject>(value)) return Type::Object;
    return Type::Null;
}

bool JsonValue::isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
bool JsonValue::isBool() const { return std::holds_alternative<bool>(value); }
bool JsonValue::isNumber() const { return std::holds_alternative<double>(value); }
bool JsonValue::isString() const { return std::holds_alternative<std::string>(value); }
bool JsonValue::isArray() const { return std::holds_alternative<JsonArray>(value); }
bool JsonValue::isObject() const { return std::holds_alternative<JsonObject>(value); }

bool *JsonValue::asBool() { return std::get_if<bool>(&value); }
double *JsonValue::asNumber() { return std::get_if<double>(&value); }
std::string *JsonValue::asString() { return std::get_if<std::string>(&value); }
JsonArray *JsonValue::asArray() { return std::get_if<JsonArray>(&value); }
JsonObject *JsonValue::asObject() { return std::get_if<JsonObject>(&value); }

const bool *JsonValue::asBool() const { return std::get_if<bool>(&value); }
const double *JsonValue::asNumber() const { return std::get_if<double>(&value); }
const std::string *JsonValue::asString() const { return std::get_if<std::string>(&value); }
const JsonArray *JsonValue::asArray() const { return std::get_if<JsonArray>(&value); }
const JsonObject *JsonValue::asObject() const { return std::get_if<JsonObject>(&value); }

JsonValue *JsonValue::get(std::string_view key) {
    if (auto obj = asObject()) {
        // std::less<> allows searching map<string> with string_view (C++14 feature)
        auto it = obj->find(key);
        if (it != obj->end()) {
            return &it->second;
        }
    }
    return nullptr;
}

const JsonValue *JsonValue::get(std::string_view key) const {
    if (auto obj = asObject()) {
        // std::less<> allows searching map<string> with string_view (C++14 feature)
        auto it = obj->find(key);
        if (it != obj->end()) {
            return &it->second;
        }
    }
    return nullptr;
}

std::optional<std::string> JsonValue::getString(std::string_view key) const {
    if (auto v = get(key)) {
        if (v->isString()) return *v->asString();
    }
    return std::nullopt;
}

std::optional<double> JsonValue::getNumber(std::string_view key) const {
    if (auto v = get(key)) {
        if (v->isNumber()) return *v->asNumber();
    }
    return std::nullopt;
}

std::optional<bool> JsonValue::getBool(std::string_view key) const {
    if (auto v = get(key)) {
        if (v->isBool()) return *v->asBool();
    }
    return std::nullopt;
}

// =========================================================
// Parser Implementation
// =========================================================

namespace {

class Parser {
public:
    Parser(std::string_view text) : text_(text), pos_(0) {}

    std::optional<JsonValue> parse(JsonParseError *err) {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            reportError(err, "Empty input");
            return std::nullopt;
        }

        auto result = parseValue();
        if (!result) {
            if (err && err->message.empty()) {
                reportError(err, "Invalid value");
            }
            return std::nullopt;
        }

        skipWhitespace();
        if (pos_ < text_.size()) {
            reportError(err, "Unexpected characters after root element");
            return std::nullopt;
        }

        return result;
    }

private:
    std::string_view text_;
    std::size_t pos_;

    void reportError(JsonParseError *err, const std::string &msg) const {
        if (!err) {
            return;
        }

        err->offset = pos_;
        err->message = msg;

        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t safe_pos = std::min(pos_, text_.size());
        for (std::size_t i = 0; i < safe_pos; ++i) {
            if (text_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        err->line = line;
        err->column = column;
    }

    void skipWhitespace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            pos_++;
        }
    }

    char peek() const {
        if (pos_ < text_.size()) return text_[pos_];
        return '\0';
    }

    char advance() {
        if (pos_ < text_.size()) return text_[pos_++];
        return '\0';
    }

    bool match(std::string_view token) {
        if (text_.substr(pos_).starts_with(token)) {
            pos_ += token.size();
            return true;
        }
        return false;
    }

    std::optional<JsonValue> parseValue() {
        skipWhitespace();
        char c = peek();

        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return parseString();
        if (c == 't') return parseTrue();
        if (c == 'f') return parseFalse();
        if (c == 'n') return parseNull();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

        return std::nullopt;
    }

    std::optional<JsonValue> parseObject() {
        advance(); // consume '{'
        skipWhitespace();

        JsonObject obj;

        if (peek() == '}') {
            advance();
            return JsonValue::makeObject(std::move(obj));
        }

        while (true) {
            if (peek() != '"') return std::nullopt;

            auto keyVal = parseString();
            if (!keyVal) return std::nullopt;

            std::string key = *keyVal->asString();

            skipWhitespace();
            if (advance() != ':') return std::nullopt;

            auto val = parseValue();
            if (!val) return std::nullopt;

            obj.emplace(std::move(key), std::move(*val));

            skipWhitespace();
            char next = peek();
            if (next == '}') {
                advance();
                break;
            } else if (next == ',') {
                advance();
                skipWhitespace();
            } else {
                return std::nullopt;
            }
        }

        return JsonValue::makeObject(std::move(obj));
    }

    std::optional<JsonValue> parseArray() {
        advance(); // consume '['
        skipWhitespace();

        JsonArray arr;

        if (peek() == ']') {
            advance();
            return JsonValue::makeArray(std::move(arr));
        }

        while (true) {
            auto val = parseValue();
            if (!val) return std::nullopt;

            arr.push_back(std::move(*val));

            skipWhitespace();
            char next = peek();
            if (next == ']') {
                advance();
                break;
            } else if (next == ',') {
                advance();
                skipWhitespace();
            } else {
                return std::nullopt;
            }
        }

        return JsonValue::makeArray(std::move(arr));
    }

    std::optional<JsonValue> parseString() {
        advance(); // consume '"'
        std::string str;
        str.reserve(32); // Optimization hint

        while (pos_ < text_.size()) {
            char c = advance();
            if (c == '"') {
                return JsonValue::makeString(std::move(str));
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) return std::nullopt;
                char escape = advance();
                switch (escape) {
                    case '"': str += '"'; break;
                    case '\\': str += '\\'; break;
                    case '/': str += '/'; break;
                    case 'b': str += '\b'; break;
                    case 'f': str += '\f'; break;
                    case 'n': str += '\n'; break;
                    case 'r': str += '\r'; break;
                    case 't': str += '\t'; break;
                    case 'u': {
                        // Minimal unicode support: just skip 4 hex digits for now
                        // or implement proper UTF-8 encoding if needed.
                        // For this skeleton, we'll try to parse 4 hex chars.
                        if (pos_ + 4 > text_.size()) return std::nullopt;
                        // TODO: Implement proper unicode to UTF-8 conversion
                        pos_ += 4;
                        str += '?'; // Placeholder for simplify
                        break;
                    }
                    default: return std::nullopt;
                }
            } else {
                str += c;
            }
        }
        return std::nullopt;
    }

    std::optional<JsonValue> parseNumber() {
        size_t start = pos_;
        // Optional minus
        if (peek() == '-') advance();

        // Integer part
        if (peek() == '0') {
            advance();
        } else if (std::isdigit(static_cast<unsigned char>(peek()))) {
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        } else {
            return std::nullopt;
        }

        // Fraction part
        if (peek() == '.') {
            advance();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) return std::nullopt;
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }

        // Exponent part
        if (peek() == 'e' || peek() == 'E') {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!std::isdigit(static_cast<unsigned char>(peek()))) return std::nullopt;
            while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }

        std::string_view numStr = text_.substr(start, pos_ - start);

        // Use std::from_chars (C++17) or std::strtod
        // Note: std::from_chars for double requires fairly recent compiler support.
        // Fallback to std::strtod with a temp string for maximum compatibility.
        std::string temp(numStr);
        char* endPtr = nullptr;
        double val = std::strtod(temp.c_str(), &endPtr);

        if (endPtr != temp.c_str() + temp.size()) {
            return std::nullopt;
        }

        return JsonValue::makeNumber(val);
    }

    std::optional<JsonValue> parseTrue() {
        if (match("true")) return JsonValue::makeBool(true);
        return std::nullopt;
    }

    std::optional<JsonValue> parseFalse() {
        if (match("false")) return JsonValue::makeBool(false);
        return std::nullopt;
    }

    std::optional<JsonValue> parseNull() {
        if (match("null")) return JsonValue::makeNull();
        return std::nullopt;
    }
};

} // namespace

static void AppendEscapedJsonString(const std::string &input, std::string *out) {
    out->push_back('"');
    for (unsigned char ch : input) {
        switch (ch) {
            case '"': out->append("\\\""); break;
            case '\\': out->append("\\\\"); break;
            case '\b': out->append("\\b"); break;
            case '\f': out->append("\\f"); break;
            case '\n': out->append("\\n"); break;
            case '\r': out->append("\\r"); break;
            case '\t': out->append("\\t"); break;
            default:
                if (ch < 0x20) {
                    char buf[7]{};
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned int>(ch));
                    out->append(buf);
                } else {
                    out->push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    out->push_back('"');
}

static void StringifyImpl(const JsonValue &value, int indent_spaces, int depth, std::string *out) {
    const auto indent = [&](int d) {
        out->append(static_cast<std::size_t>(std::max(0, d * indent_spaces)), ' ');
    };

    switch (value.type()) {
        case JsonValue::Type::Null:
            out->append("null");
            return;
        case JsonValue::Type::Bool:
            out->append((*value.asBool()) ? "true" : "false");
            return;
        case JsonValue::Type::Number: {
            char num_buf[64]{};
            std::snprintf(num_buf, sizeof(num_buf), "%.15g", *value.asNumber());
            out->append(num_buf);
            return;
        }
        case JsonValue::Type::String:
            AppendEscapedJsonString(*value.asString(), out);
            return;
        case JsonValue::Type::Array: {
            const JsonArray *arr = value.asArray();
            if (!arr || arr->empty()) {
                out->append("[]");
                return;
            }
            out->append("[\n");
            for (std::size_t i = 0; i < arr->size(); ++i) {
                indent(depth + 1);
                StringifyImpl((*arr)[i], indent_spaces, depth + 1, out);
                if (i + 1 < arr->size()) {
                    out->append(",");
                }
                out->append("\n");
            }
            indent(depth);
            out->append("]");
            return;
        }
        case JsonValue::Type::Object: {
            const JsonObject *obj = value.asObject();
            if (!obj || obj->empty()) {
                out->append("{}");
                return;
            }
            out->append("{\n");
            std::size_t i = 0;
            for (const auto &kv : *obj) {
                indent(depth + 1);
                AppendEscapedJsonString(kv.first, out);
                out->append(": ");
                StringifyImpl(kv.second, indent_spaces, depth + 1, out);
                if (i + 1 < obj->size()) {
                    out->append(",");
                }
                out->append("\n");
                ++i;
            }
            indent(depth);
            out->append("}");
            return;
        }
    }
}

std::optional<JsonValue> ParseJson(std::string_view text, JsonParseError *outError) {
    Parser parser(text);
    return parser.parse(outError);
}

std::string StringifyJson(const JsonValue &value, int indent_spaces) {
    if (indent_spaces <= 0) {
        indent_spaces = 2;
    }
    std::string out;
    StringifyImpl(value, indent_spaces, 0, &out);
    return out;
}

}  // namespace k2d
