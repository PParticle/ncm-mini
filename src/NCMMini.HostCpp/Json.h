#pragma once

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ncmmini
{
class JsonValue
{
public:
    using Object = std::vector<std::pair<std::string, JsonValue>>;
    using Array = std::vector<JsonValue>;
    using Storage = std::variant<std::monostate, bool, double, std::string, Object, Array>;

    JsonValue() = default;
    explicit JsonValue(bool value) : value_(value) {}
    explicit JsonValue(double value) : value_(value) {}
    explicit JsonValue(std::string value) : value_(std::move(value)) {}
    explicit JsonValue(Object value) : value_(std::move(value)) {}
    explicit JsonValue(Array value) : value_(std::move(value)) {}

    const std::string* AsString() const;
    const double* AsNumber() const;
    const Object* AsObject() const;
    const Array* AsArray() const;
    const JsonValue* Find(const std::string& name) const;

private:
    Storage value_;
};

bool ParseJson(const std::string& text, JsonValue& value);
}
