#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace xmf::json {

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool b) : data_(b) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(int64_t i) : data_(static_cast<double>(i)) {}
    Value(uint32_t i) : data_(static_cast<double>(i)) {}
    Value(double d) : data_(d) {}
    Value(const char* s) : data_(std::string(s ? s : "")) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(Array a) : data_(std::move(a)) {}
    Value(Object o) : data_(std::move(o)) {}

    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool IsBool() const { return std::holds_alternative<bool>(data_); }
    bool IsNumber() const { return std::holds_alternative<double>(data_); }
    bool IsString() const { return std::holds_alternative<std::string>(data_); }
    bool IsArray() const { return std::holds_alternative<Array>(data_); }
    bool IsObject() const { return std::holds_alternative<Object>(data_); }

    bool AsBool(bool def = false) const { return IsBool() ? std::get<bool>(data_) : def; }
    double AsDouble(double def = 0.0) const { return IsNumber() ? std::get<double>(data_) : def; }
    int AsInt(int def = 0) const { return IsNumber() ? static_cast<int>(std::get<double>(data_)) : def; }
    int64_t AsInt64(int64_t def = 0) const { return IsNumber() ? static_cast<int64_t>(std::get<double>(data_)) : def; }
    const std::string& AsString() const {
        static const std::string empty;
        return IsString() ? std::get<std::string>(data_) : empty;
    }
    std::string AsStringOr(const std::string& def) const {
        return IsString() ? std::get<std::string>(data_) : def;
    }
    const Array& AsArray() const {
        static const Array empty;
        return IsArray() ? std::get<Array>(data_) : empty;
    }
    Array& MutableArray() {
        if (!IsArray()) data_ = Array{};
        return std::get<Array>(data_);
    }
    const Object& AsObject() const {
        static const Object empty;
        return IsObject() ? std::get<Object>(data_) : empty;
    }
    Object& MutableObject() {
        if (!IsObject()) data_ = Object{};
        return std::get<Object>(data_);
    }

    bool Contains(const std::string& key) const {
        if (!IsObject()) return false;
        const auto& obj = std::get<Object>(data_);
        return obj.find(key) != obj.end();
    }

    const Value& Get(const std::string& key) const {
        static const Value nullVal;
        if (!IsObject()) return nullVal;
        const auto& obj = std::get<Object>(data_);
        auto it = obj.find(key);
        return it != obj.end() ? it->second : nullVal;
    }

    Value& operator[](const std::string& key) {
        return MutableObject()[key];
    }

    std::string Dump(int indent = 2) const {
        std::ostringstream oss;
        DumpImpl(oss, indent, 0);
        return oss.str();
    }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;

    static void EscapeString(std::ostringstream& oss, const std::string& s) {
        oss << '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\b': oss << "\\b"; break;
            case '\f': oss << "\\f"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    oss << buf;
                } else {
                    oss << static_cast<char>(c);
                }
                break;
            }
        }
        oss << '"';
    }

    void DumpImpl(std::ostringstream& oss, int indent, int depth) const {
        if (IsNull()) {
            oss << "null";
        } else if (IsBool()) {
            oss << (std::get<bool>(data_) ? "true" : "false");
        } else if (IsNumber()) {
            double d = std::get<double>(data_);
            int64_t i = static_cast<int64_t>(d);
            if (static_cast<double>(i) == d) {
                oss << i;
            } else {
                oss << d;
            }
        } else if (IsString()) {
            EscapeString(oss, std::get<std::string>(data_));
        } else if (IsArray()) {
            const auto& arr = std::get<Array>(data_);
            if (arr.empty()) {
                oss << "[]";
                return;
            }
            oss << '[';
            if (indent >= 0) oss << '\n';
            for (size_t i = 0; i < arr.size(); ++i) {
                if (indent >= 0) oss << std::string((depth + 1) * indent, ' ');
                arr[i].DumpImpl(oss, indent, depth + 1);
                if (i + 1 < arr.size()) oss << ',';
                if (indent >= 0) oss << '\n';
            }
            if (indent >= 0) oss << std::string(depth * indent, ' ');
            oss << ']';
        } else if (IsObject()) {
            const auto& obj = std::get<Object>(data_);
            if (obj.empty()) {
                oss << "{}";
                return;
            }
            oss << '{';
            if (indent >= 0) oss << '\n';
            size_t count = 0;
            for (const auto& [k, v] : obj) {
                if (indent >= 0) oss << std::string((depth + 1) * indent, ' ');
                EscapeString(oss, k);
                oss << (indent >= 0 ? ": " : ":");
                v.DumpImpl(oss, indent, depth + 1);
                if (++count < obj.size()) oss << ',';
                if (indent >= 0) oss << '\n';
            }
            if (indent >= 0) oss << std::string(depth * indent, ' ');
            oss << '}';
        }
    }
};

class Parser {
public:
    explicit Parser(const std::string& input) : s_(input), pos_(0) {}

    bool Parse(Value& out) {
        // Strip UTF-8 BOM if present
        if (s_.size() >= 3 &&
            static_cast<unsigned char>(s_[0]) == 0xEF &&
            static_cast<unsigned char>(s_[1]) == 0xBB &&
            static_cast<unsigned char>(s_[2]) == 0xBF) {
            pos_ = 3;
        }
        SkipWs();
        if (!ParseValue(out)) return false;
        SkipWs();
        return pos_ == s_.size();
    }

private:
    const std::string& s_;
    size_t pos_;

    void SkipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\r' || s_[pos_] == '\n')) {
            ++pos_;
        }
    }

    bool ParseValue(Value& out) {
        SkipWs();
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == 'n') return ParseLiteral("null", Value(nullptr), out);
        if (c == 't') return ParseLiteral("true", Value(true), out);
        if (c == 'f') return ParseLiteral("false", Value(false), out);
        if (c == '"') {
            std::string str;
            if (!ParseString(str)) return false;
            out = Value(std::move(str));
            return true;
        }
        if (c == '[') return ParseArray(out);
        if (c == '{') return ParseObject(out);
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return false;
    }

    bool ParseLiteral(const char* lit, Value val, Value& out) {
        size_t len = std::char_traits<char>::length(lit);
        if (s_.compare(pos_, len, lit) == 0) {
            pos_ += len;
            out = std::move(val);
            return true;
        }
        return false;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(uint32_t& code) {
        if (pos_ + 4 > s_.size()) return false;
        code = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= (c - '0');
            else if (c >= 'a' && c <= 'f') code |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= (c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& out) {
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        ++pos_;
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) return false;
                char e = s_[pos_++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!ParseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 2 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!ParseHex4(low)) return false;
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return false;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(Value& out) {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        try {
            double val = std::stod(s_.substr(start, pos_ - start));
            out = Value(val);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ParseArray(Value& out) {
        ++pos_; // skip '['
        Value::Array arr;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = Value(std::move(arr));
            return true;
        }
        while (pos_ < s_.size()) {
            Value elem;
            if (!ParseValue(elem)) return false;
            arr.push_back(std::move(elem));
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ']') {
                ++pos_;
                out = Value(std::move(arr));
                return true;
            }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }

    bool ParseObject(Value& out) {
        ++pos_; // skip '{'
        Value::Object obj;
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = Value(std::move(obj));
            return true;
        }
        while (pos_ < s_.size()) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            Value val;
            if (!ParseValue(val)) return false;
            obj[std::move(key)] = std::move(val);
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == '}') {
                ++pos_;
                out = Value(std::move(obj));
                return true;
            }
            if (s_[pos_] != ',') return false;
            ++pos_;
        }
        return false;
    }
};

inline bool Parse(const std::string& input, Value& out) {
    Parser p(input);
    return p.Parse(out);
}

} // namespace xmf::json
