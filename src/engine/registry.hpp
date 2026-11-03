// registry.hpp - engine state that scripts can reach by name: engine.get("pet.speedMul"), engine.set("pet.speedMul", 1.5).
//
// The game registers a value with one line (expose("name", &variable), or exposeFn for a computed one). Nothing here knows
// about Lua; script_host.hpp binds it. Anything registered can be read AND written: a mod that sets a bad value breaks its own
// game, and that is the modder's to find (the registry only refuses what could not be stored: the wrong kind of value, or a number
// outside the variable's type).
//
// Names are lowercase-friendly dotted paths ("pet.speedMul"): letters, digits, '_' and '.', 1 to 64 characters.
#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace gp
{
    struct EngineValue
    {
        enum class Kind
        {
            Int,
            Real,
            Bool,
            Str
        };
        Kind kind = Kind::Int;
        long long i = 0;
        double r = 0;
        bool b = false;
        std::string s;

        static EngineValue ofInt(long long v)
        {
            EngineValue e;
            e.kind = Kind::Int;
            e.i = v;
            return e;
        }
        static EngineValue ofReal(double v)
        {
            EngineValue e;
            e.kind = Kind::Real;
            e.r = v;
            return e;
        }
        static EngineValue ofBool(bool v)
        {
            EngineValue e;
            e.kind = Kind::Bool;
            e.b = v;
            return e;
        }
        static EngineValue ofStr(std::string v)
        {
            EngineValue e;
            e.kind = Kind::Str;
            e.s = std::move(v);
            return e;
        }
    };

    class EngineRegistry
    {
    public:
        // set returns "" when the value was taken, otherwise why not (shown to the modder).
        using Getter = std::function<EngineValue()>;
        using Setter = std::function<std::string(const EngineValue &)>;

        static bool validName(const std::string &n)
        {
            if (n.empty() || n.size() > 64)
                return false;
            for (unsigned char c : n)
                if (!(std::isalnum(c) || c == '_' || c == '.'))
                    return false;
            return true;
        }

        // A variable of the game. Integers (any width, signed or not), floating point, bool and std::string.
        // The pointer must stay valid for as long as the registry is used. Exposing a name again replaces it.
        template <class T>
        void expose(const std::string &name, T *p, const std::string &note = "")
        {
            Getter g;
            Setter s;
            const char *kind;
            if constexpr (std::is_same_v<T, bool>)
            {
                kind = "bool";
                g = [p] { return EngineValue::ofBool(*p); };
                s = [p](const EngineValue &v) -> std::string
                {
                    if (v.kind != EngineValue::Kind::Bool)
                        return "expects true or false";
                    *p = v.b;
                    return "";
                };
            }
            else if constexpr (std::is_integral_v<T>)
            {
                kind = "int";
                g = [p] { return EngineValue::ofInt(static_cast<long long>(*p)); };
                s = [p](const EngineValue &v) -> std::string
                {
                    long long x;
                    if (v.kind == EngineValue::Kind::Int)
                        x = v.i;
                    else if (v.kind == EngineValue::Kind::Real && std::isfinite(v.r) && v.r == std::floor(v.r) && std::fabs(v.r) < 9e18)
                        x = static_cast<long long>(v.r);
                    else
                        return "expects a whole number";
                    if (x < static_cast<long long>(std::numeric_limits<T>::min()) ||
                        (x > 0 && static_cast<unsigned long long>(x) > static_cast<unsigned long long>(std::numeric_limits<T>::max())))
                        return "is outside what this value can hold (" + std::to_string(std::numeric_limits<T>::min()) + " to " +
                               std::to_string(std::numeric_limits<T>::max()) + ")";
                    *p = static_cast<T>(x);
                    return "";
                };
            }
            else if constexpr (std::is_floating_point_v<T>)
            {
                kind = "number";
                g = [p] { return EngineValue::ofReal(static_cast<double>(*p)); };
                s = [p](const EngineValue &v) -> std::string
                {
                    if (v.kind == EngineValue::Kind::Int)
                        *p = static_cast<T>(v.i);
                    else if (v.kind == EngineValue::Kind::Real)
                        *p = static_cast<T>(v.r);
                    else
                        return "expects a number";
                    return "";
                };
            }
            else
            {
                static_assert(std::is_same_v<T, std::string>, "expose: bool, integer, floating point or std::string");
                kind = "string";
                g = [p] { return EngineValue::ofStr(*p); };
                s = [p](const EngineValue &v) -> std::string
                {
                    if (v.kind != EngineValue::Kind::Str)
                        return "expects a string";
                    *p = v.s;
                    return "";
                };
            }
            put(name, kind, std::move(g), std::move(s), note);
        }

        // A computed value. `kind` is only a label for engine.list ("int", "number", "bool", "string"). An empty setter makes it
        // read-only (only for values that really are computed; a plain variable should be exposed with expose).
        void exposeFn(const std::string &name, const std::string &kind, Getter get, Setter set = {}, const std::string &note = "")
        {
            put(name, kind, std::move(get), std::move(set), note);
        }

        bool has(const std::string &name) const { return e_.count(name) != 0; }

        std::optional<EngineValue> get(const std::string &name) const
        {
            const auto it = e_.find(name);
            if (it == e_.end())
                return std::nullopt;
            return it->second.get();
        }

        // "" = stored; otherwise the reason, e.g. "'pet.speedMul' expects a number".
        std::string set(const std::string &name, const EngineValue &v) const
        {
            const auto it = e_.find(name);
            if (it == e_.end())
                return "no engine value '" + name + "'";
            if (!it->second.set)
                return "'" + name + "' is read-only";
            const std::string why = it->second.set(v);
            return why.empty() ? why : "'" + name + "' " + why;
        }

        // Names starting with `prefix`, sorted.
        std::vector<std::string> names(const std::string &prefix = "") const
        {
            std::vector<std::string> out;
            for (const auto &[n, v] : e_)
                if (n.compare(0, prefix.size(), prefix) == 0)
                    out.push_back(n);
            return out;
        }
        std::string kindOf(const std::string &name) const
        {
            const auto it = e_.find(name);
            return it == e_.end() ? "" : it->second.kind;
        }
        std::string noteOf(const std::string &name) const
        {
            const auto it = e_.find(name);
            return it == e_.end() ? "" : it->second.note;
        }
        size_t size() const { return e_.size(); }

    private:
        struct Entry
        {
            std::string kind, note;
            Getter get;
            Setter set;
        };
        void put(const std::string &name, const std::string &kind, Getter g, Setter s, const std::string &note)
        {
            if (!validName(name))
                return; // a programming error in the game's own registration; the name list stays as it was
            e_[name] = Entry{kind, note, std::move(g), std::move(s)};
        }
        std::map<std::string, Entry> e_;
    };
} // namespace gp
