#include <nxfire/Json.h>

#include <cmath>
#include <cstdio>
#include <sstream>

namespace nxfire {

namespace {

void escapeTo(std::string& out, const std::string& s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
}

void dumpTo(std::string& out, const Json& v);

void dumpArray(std::string& out, const Array& a) {
    out.push_back('[');
    for (size_t i = 0; i < a.size(); ++i) {
        if (i) out.push_back(',');
        dumpTo(out, a[i]);
    }
    out.push_back(']');
}

void dumpObject(std::string& out, const Object& o) {
    out.push_back('{');
    size_t i = 0;
    for (const auto& kv : o) {
        if (i++) out.push_back(',');
        escapeTo(out, kv.first);
        out.push_back(':');
        dumpTo(out, kv.second);
    }
    out.push_back('}');
}

void dumpTo(std::string& out, const Json& v) {
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Json::Type::Int: out += std::to_string(v.asInt()); break;
        case Json::Type::Real: {
            double d = v.asReal();
            if (std::isfinite(d)) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", d);
                out += buf;
            } else {
                out += "null";
            }
            break;
        }
        case Json::Type::String: escapeTo(out, v.asString()); break;
        case Json::Type::Array: dumpArray(out, v.asArray()); break;
        case Json::Type::Object: dumpObject(out, v.asObject()); break;
    }
}

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string* err;

    Parser(const std::string& text, std::string* error) : s(text), err(error) {}

    void skipWs() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }

    bool fail(const std::string& msg) {
        if (err) *err = msg + " (pos " + std::to_string(i) + ")";
        return false;
    }

    bool parseValue(Json& out) {
        skipWs();
        if (i >= s.size()) return fail("input berakhir");
        char c = s[i];
        if (c == '{') return parseObject(out);
        if (c == '[') return parseArray(out);
        if (c == '"') {
            std::string v;
            if (!parseString(v)) return false;
            out = Json(v);
            return true;
        }
        if (c == 't') return literal("true", out, Json(true));
        if (c == 'f') return literal("false", out, Json(false));
        if (c == 'n') return literal("null", out, Json(nullptr));
        return parseNumber(out);
    }

    bool literal(const char* word, Json& out, Json value) {
        size_t len = std::char_traits<char>::length(word);
        if (s.compare(i, len, word) != 0) return fail("token tidak dikenal");
        i += len;
        out = value;
        return true;
    }

    bool parseString(std::string& out) {
        if (s[i] != '"') return fail("mengharapkan string");
        ++i;
        out.clear();
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return true;
            if (c == '\\') {
                if (i >= s.size()) break;
                char e = s[i++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'u': {
                        if (i + 4 > s.size()) return fail("\\u tidak lengkap");
                        unsigned code = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= unsigned(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= unsigned(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= unsigned(h - 'A' + 10);
                            else return fail("digit \\u tidak valid");
                        }
                        if (code < 0x80) {
                            out.push_back(char(code));
                        } else if (code < 0x800) {
                            out.push_back(char(0xC0 | (code >> 6)));
                            out.push_back(char(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(char(0xE0 | (code >> 12)));
                            out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(char(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: return fail("escape tidak valid");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("string tidak ditutup");
    }

    bool parseNumber(Json& out) {
        size_t start = i;
        bool isReal = false;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        if (i < s.size() && s[i] == '.') {
            isReal = true;
            ++i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            isReal = true;
            ++i;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        }
        if (i == start) return fail("angka tidak valid");
        std::string num = s.substr(start, i - start);
        try {
            if (isReal) out = Json(std::stod(num));
            else out = Json(int64_t(std::stoll(num)));
        } catch (...) {
            return fail("angka di luar jangkauan");
        }
        return true;
    }

    bool parseArray(Json& out) {
        ++i;
        Json arr = Json::array();
        skipWs();
        if (i < s.size() && s[i] == ']') { ++i; out = arr; return true; }
        while (true) {
            Json v;
            if (!parseValue(v)) return false;
            arr.push_back(v);
            skipWs();
            if (i >= s.size()) return fail("array tidak ditutup");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; break; }
            return fail("mengharapkan ',' atau ']'");
        }
        out = arr;
        return true;
    }

    bool parseObject(Json& out) {
        ++i;
        Json obj = Json::object();
        skipWs();
        if (i < s.size() && s[i] == '}') { ++i; out = obj; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (i >= s.size() || s[i] != '"') return fail("mengharapkan nama kunci");
            if (!parseString(key)) return false;
            skipWs();
            if (i >= s.size() || s[i] != ':') return fail("mengharapkan ':'");
            ++i;
            Json v;
            if (!parseValue(v)) return false;
            obj.set(key, v);
            skipWs();
            if (i >= s.size()) return fail("object tidak ditutup");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            return fail("mengharapkan ',' atau '}'");
        }
        out = obj;
        return true;
    }
};

} // namespace

Json::Json() = default;
Json::Json(std::nullptr_t) : type_(Type::Null) {}
Json::Json(bool v) : type_(Type::Bool), bool_(v) {}
Json::Json(int v) : type_(Type::Int), int_(v) {}
Json::Json(int64_t v) : type_(Type::Int), int_(v) {}
Json::Json(double v) : type_(Type::Real), real_(v) {}
Json::Json(const char* v) : type_(Type::String), string_(v ? v : "") {}
Json::Json(const std::string& v) : type_(Type::String), string_(v) {}
Json::Json(Array v) : type_(Type::Array), array_(std::move(v)) {}
Json::Json(Object v) : type_(Type::Object), object_(std::move(v)) {}

Json Json::object() { return Json(Object{}); }
Json Json::array() { return Json(Array{}); }

bool Json::asBool(bool def) const {
    if (type_ == Type::Bool) return bool_;
    if (type_ == Type::Int) return int_ != 0;
    return def;
}

int64_t Json::asInt(int64_t def) const {
    if (type_ == Type::Int) return int_;
    if (type_ == Type::Real) return int64_t(real_);
    if (type_ == Type::Bool) return bool_ ? 1 : 0;
    return def;
}

double Json::asReal(double def) const {
    if (type_ == Type::Real) return real_;
    if (type_ == Type::Int) return double(int_);
    return def;
}

std::string Json::asString(const std::string& def) const {
    if (type_ == Type::String) return string_;
    return def;
}

bool Json::contains(const std::string& key) const {
    return type_ == Type::Object && object_.count(key) > 0;
}

Json& Json::operator[](const std::string& key) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    return object_[key];
}

const Json& Json::at(const std::string& key) const {
    static const Json null;
    auto it = object_.find(key);
    return it == object_.end() ? null : it->second;
}

void Json::set(const std::string& key, Json value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    object_[key] = std::move(value);
}

void Json::push_back(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
}

size_t Json::size() const {
    if (type_ == Type::Array) return array_.size();
    if (type_ == Type::Object) return object_.size();
    return 0;
}

std::string Json::dump() const {
    std::string out;
    dumpTo(out, *this);
    return out;
}

Json Json::parse(const std::string& text) {
    return parse(text, nullptr);
}

Json Json::parse(const std::string& text, std::string* error) {
    if (error) error->clear();
    Parser p(text, error);
    Json out;
    if (!p.parseValue(out)) return Json();
    p.skipWs();
    if (p.i != text.size()) {
        if (error) *error = "ada karakter tersisa setelah JSON";
        return Json();
    }
    return out;
}

} // namespace nxfire
