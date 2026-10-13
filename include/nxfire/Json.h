#ifndef NXFIRE_JSON_H
#define NXFIRE_JSON_H

#include <nxfire/Export.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nxfire {

class Json;

using Array  = std::vector<Json>;
using Object = std::map<std::string, Json>;

// Nilai JSON generik — dipakai di seluruh NxfireDB.
class NXFIRE_EXPORT Json {
public:
    enum class Type { Null, Bool, Int, Real, String, Array, Object };

    Json();
    Json(std::nullptr_t);
    Json(bool v);
    Json(int v);
    Json(int64_t v);
    Json(double v);
    Json(const char* v);
    Json(const std::string& v);
    Json(Array v);
    Json(Object v);

    static Json object();
    static Json array();

    Type type() const { return type_; }

    bool isNull()   const { return type_ == Type::Null;   }
    bool isBool()   const { return type_ == Type::Bool;   }
    bool isInt()    const { return type_ == Type::Int;    }
    bool isReal()   const { return type_ == Type::Real;   }
    bool isNumber() const { return isInt() || isReal();   }
    bool isString() const { return type_ == Type::String; }
    bool isArray()  const { return type_ == Type::Array;  }
    bool isObject() const { return type_ == Type::Object; }

    bool        asBool  (bool               def = false) const;
    int64_t     asInt   (int64_t            def = 0)     const;
    double      asReal  (double             def = 0.0)   const;
    std::string asString(const std::string& def = {})    const;

    bool        contains(const std::string& key) const;
    Json&       operator[](const std::string& key);
    const Json& at(const std::string& key) const;
    void        set(const std::string& key, Json value);
    const Object& asObject() const { return object_; }

    Array&       asArray()       { return array_; }
    const Array& asArray() const { return array_; }
    void         push_back(Json value);
    size_t       size() const;

    std::string dump() const;
    static Json parse(const std::string& text);
    static Json parse(const std::string& text, std::string* error);

private:
    Type    type_   = Type::Null;
    bool    bool_   = false;
    int64_t int_    = 0;
    double  real_   = 0.0;
    std::string string_;
    Array   array_;
    Object  object_;
};

} // namespace nxfire

#endif // NXFIRE_JSON_H
