#include "Json.h"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace ncmmini
{
const std::string* JsonValue::AsString() const
{
    return std::get_if<std::string>(&value_);
}

const double* JsonValue::AsNumber() const
{
    return std::get_if<double>(&value_);
}

const JsonValue::Object* JsonValue::AsObject() const
{
    return std::get_if<Object>(&value_);
}

const JsonValue::Array* JsonValue::AsArray() const
{
    return std::get_if<Array>(&value_);
}

const JsonValue* JsonValue::Find(const std::string& name) const
{
    const auto* object = AsObject();
    if (object == nullptr)
    {
        return nullptr;
    }
    for (const auto& [key, value] : *object)
    {
        if (key == name)
        {
            return &value;
        }
    }
    return nullptr;
}

class JsonParser
{
public:
    explicit JsonParser(const std::string& text) : text_(text) {}

    bool Parse(JsonValue& value)
    {
        SkipSpace();
        if (!ParseValue(value))
        {
            return false;
        }
        SkipSpace();
        return position_ == text_.size();
    }

private:
    bool ParseValue(JsonValue& value)
    {
        if (position_ >= text_.size())
        {
            return false;
        }
        switch (text_[position_])
        {
        case '{':
            return ParseObject(value);
        case '[':
            return ParseArray(value);
        case '"':
        {
            std::string text;
            if (!ParseString(text))
            {
                return false;
            }
            value = JsonValue(std::move(text));
            return true;
        }
        case 't':
            return ParseLiteral("true", JsonValue(true), value);
        case 'f':
            return ParseLiteral("false", JsonValue(false), value);
        case 'n':
            return ParseLiteral("null", JsonValue(), value);
        default:
            return ParseNumber(value);
        }
    }

    bool ParseObject(JsonValue& value)
    {
        ++position_;
        SkipSpace();
        JsonValue::Object object;
        if (Consume('}'))
        {
            value = JsonValue(std::move(object));
            return true;
        }
        while (position_ < text_.size())
        {
            std::string name;
            if (!ParseString(name))
            {
                return false;
            }
            SkipSpace();
            if (!Consume(':'))
            {
                return false;
            }
            SkipSpace();
            JsonValue child;
            if (!ParseValue(child))
            {
                return false;
            }
            object.emplace_back(std::move(name), std::move(child));
            SkipSpace();
            if (Consume('}'))
            {
                value = JsonValue(std::move(object));
                return true;
            }
            if (!Consume(','))
            {
                return false;
            }
            SkipSpace();
        }
        return false;
    }

    bool ParseArray(JsonValue& value)
    {
        ++position_;
        SkipSpace();
        JsonValue::Array array;
        if (Consume(']'))
        {
            value = JsonValue(std::move(array));
            return true;
        }
        while (position_ < text_.size())
        {
            JsonValue child;
            if (!ParseValue(child))
            {
                return false;
            }
            array.emplace_back(std::move(child));
            SkipSpace();
            if (Consume(']'))
            {
                value = JsonValue(std::move(array));
                return true;
            }
            if (!Consume(','))
            {
                return false;
            }
            SkipSpace();
        }
        return false;
    }

    bool ParseString(std::string& result)
    {
        if (!Consume('"'))
        {
            return false;
        }
        result.clear();
        while (position_ < text_.size())
        {
            const auto character = static_cast<unsigned char>(text_[position_++]);
            if (character == '"')
            {
                return true;
            }
            if (character < 0x20)
            {
                return false;
            }
            if (character != '\\')
            {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= text_.size())
            {
                return false;
            }
            switch (text_[position_++])
            {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codePoint = 0;
                if (!ParseHex(codePoint))
                {
                    return false;
                }
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                {
                    if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
                    {
                        return false;
                    }
                    position_ += 2;
                    std::uint32_t low = 0;
                    if (!ParseHex(low) || low < 0xDC00 || low > 0xDFFF)
                    {
                        return false;
                    }
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                }
                AppendUtf8(codePoint, result);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool ParseHex(std::uint32_t& value)
    {
        if (position_ + 4 > text_.size())
        {
            return false;
        }
        value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const auto character = text_[position_++];
            value <<= 4;
            if (character >= '0' && character <= '9') value += character - '0';
            else if (character >= 'a' && character <= 'f') value += character - 'a' + 10;
            else if (character >= 'A' && character <= 'F') value += character - 'A' + 10;
            else return false;
        }
        return true;
    }

    static void AppendUtf8(std::uint32_t codePoint, std::string& text)
    {
        if (codePoint <= 0x7F)
        {
            text.push_back(static_cast<char>(codePoint));
        }
        else if (codePoint <= 0x7FF)
        {
            text.push_back(static_cast<char>(0xC0 | (codePoint >> 6)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else if (codePoint <= 0xFFFF)
        {
            text.push_back(static_cast<char>(0xE0 | (codePoint >> 12)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
        else
        {
            text.push_back(static_cast<char>(0xF0 | (codePoint >> 18)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F)));
            text.push_back(static_cast<char>(0x80 | (codePoint & 0x3F)));
        }
    }

    bool ParseNumber(JsonValue& value)
    {
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        if (position_ >= text_.size()) return false;
        if (text_[position_] == '0') ++position_;
        else
        {
            if (text_[position_] < '1' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.')
        {
            ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E'))
        {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') return false;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        const auto number = std::strtod(text_.c_str() + start, nullptr);
        value = JsonValue(number);
        return true;
    }

    bool ParseLiteral(const char* literal, JsonValue parsed, JsonValue& value)
    {
        const std::string expected(literal);
        if (text_.compare(position_, expected.size(), expected) != 0)
        {
            return false;
        }
        position_ += expected.size();
        value = std::move(parsed);
        return true;
    }

    void SkipSpace()
    {
        while (position_ < text_.size()
            && (text_[position_] == ' ' || text_[position_] == '\t' || text_[position_] == '\r' || text_[position_] == '\n'))
        {
            ++position_;
        }
    }

    bool Consume(char character)
    {
        if (position_ >= text_.size() || text_[position_] != character)
        {
            return false;
        }
        ++position_;
        return true;
    }

    const std::string& text_;
    std::size_t position_ = 0;
};

bool ParseJson(const std::string& text, JsonValue& value)
{
    return JsonParser(text).Parse(value);
}
}
