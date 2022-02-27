/*  Copyright (C) 2016 Eric Wasylishen

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

 See file, 'COPYING', for details.
 */

#pragma once

#include <common/entdata.h>
#include <common/log.hh>
#include <common/qvec.hh>
#include <common/parser.hh>

#include <vector>
#include <string>
#include <cassert>
#include <sstream>
#include <map>
#include <set>
#include <limits>
#include <optional>

namespace settings
{
struct parse_exception : public std::exception
{
private:
    std::string _what;

public:
    parse_exception(std::string str) : _what(std::move(str)) { }

    const char *what() const override { return _what.c_str(); }
};

enum class source
{
    DEFAULT = 0,
    MAP = 1,
    COMMANDLINE = 2
};

class nameset : public std::vector<std::string>
{
public:
    nameset(const char *str) : vector<std::string>({str}) { }
    nameset(const std::string &str) : vector<std::string>({str}) { }
    nameset(const std::initializer_list<const char *> &strs) : vector(strs.begin(), strs.end()) { }
    nameset(const std::initializer_list<std::string> &strs) : vector(strs) { }
};

struct setting_group
{
    const char *name;
    const int32_t order;
};

class setting_container;

// base class for any setting
class setting_base
{
protected:
    source _source = source::DEFAULT;
    nameset _names;
    const setting_group *_group;
    const char *_description;

    setting_base(
        setting_container *dictionary, const nameset &names, const setting_group *group, const char *description);

    constexpr bool changeSource(source newSource)
    {
        if (newSource >= _source) {
            _source = newSource;
            return true;
        }
        return false;
    }

    // convenience function for parsing a whole string
    inline std::optional<std::string> parseString(parser_base_t &parser)
    {
        // peek the first token, if it was
        // a quoted string we can exit now
        if (!parser.parse_token(PARSE_PEEK)) {
            return std::nullopt;
        }

        if (parser.was_quoted) {
            parser.parse_token();
            return parser.token;
        }

        std::string value;

        // not a quoted string, so everything will be literal.
        // go until we reach a -.
        while (true) {
            if (parser.token[0] == '-') {
                break;
            }

            if (!value.empty()) {
                value += ' ';
            }

            value += parser.token;

            parser.parse_token();

            if (!parser.parse_token(PARSE_PEEK)) {
                break;
            }
        }

        while (std::isspace(value.back())) {
            value.pop_back();
        }
        while (std::isspace(value.front())) {
            value.erase(value.begin());
        }

        return std::move(value);
    }

public:
    inline const std::string &primaryName() const { return _names.at(0); }
    inline const nameset &names() const { return _names; }
    inline const setting_group *getGroup() const { return _group; }
    inline const char *getDescription() const { return _description; }

    constexpr bool isChanged() const { return _source != source::DEFAULT; }
    constexpr bool isLocked() const { return _source == source::COMMANDLINE; }

    constexpr const char *sourceString() const
    {
        switch (_source) {
            case source::DEFAULT: return "default";
            case source::MAP: return "map";
            case source::COMMANDLINE: return "commandline";
            default: FError("Error: unknown setting source");
        }
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) = 0;
    virtual std::string stringValue() const = 0;
    virtual std::string format() const = 0;
};

// a special type of setting that acts as a flag but
// calls back to a function to actually do the tasks.
// be careful because this won't show up in summary.
class setting_func : public setting_base
{
protected:
    std::function<void()> _func;

public:
    inline setting_func(setting_container *dictionary, const nameset &names, std::function<void()> func,
        const setting_group *group = nullptr, const char *description = "")
        : setting_base(dictionary, "test", group, description), _func(func)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        _func();
        return true;
    }

    virtual std::string stringValue() const override { return ""; }

    virtual std::string format() const override { return ""; }
};

// base class for a setting that has its own value
template<typename T>
class setting_value : public setting_base
{
protected:
    T _value;

    virtual void setValueInternal(T value, source newSource)
    {
        if (changeSource(newSource)) {
            _value = value;
        }
    }

    inline void setValueFromParse(T value, bool locked)
    {
        if (locked) {
            setValueLocked(value);
        } else {
            setValue(value);
        }
    }

public:
    inline setting_value(setting_container *dictionary, const nameset &names, T v, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description), _value(v)
    {
    }

    const T &value() const { return _value; }

    inline void setValueLocked(T f) { setValueInternal(f, source::COMMANDLINE); }

    inline void setValue(T f) { setValueInternal(f, source::MAP); }
};

class setting_bool : public setting_value<bool>
{
private:
    bool _default;

protected:
    bool parseInternal(parser_base_t &parser, bool locked, bool truthValue)
    {
        // boolean flags can be just flagged themselves
        if (parser.parse_token(PARSE_PEEK)) {
            // if the token that follows is 1, 0 or -1, we'll handle it
            // as a value, otherwise it's probably part of the next option.
            if (parser.token == "1" || parser.token == "0" || parser.token == "-1") {
                parser.parse_token();

                int intval = std::stoi(parser.token);

                const bool f = (intval != 0 && intval != -1) ? truthValue : !truthValue; // treat 0 and -1 as false

                setValueFromParse(f, locked);

                return true;
            }
        }

        setValueFromParse(truthValue, locked);

        return true;
    }

public:
    inline setting_bool(setting_container *dictionary, const nameset &names, bool v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description), _default(v)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, true);
    }

    virtual std::string stringValue() const override { return _value ? "1" : "0"; }

    virtual std::string format() const override { return _default ? "[0]" : ""; }
};

// an extension to setting_bool; this automatically adds "no" versions
// to the list, and will allow them to be used to act as `-name 0`.
class setting_invertible_bool : public setting_bool
{
private:
    nameset extendNames(const nameset &names)
    {
        nameset n = names;

        for (auto &name : names) {
            n.push_back("no" + name);
        }

        return n;
    }

public:
    inline setting_invertible_bool(setting_container *dictionary, const nameset &names, bool v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_bool(dictionary, extendNames(names), v, group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        return parseInternal(parser, locked, settingName.compare(0, 2, "no") == 0 ? false : true);
    }
};

class setting_redirect : public setting_base
{
private:
    std::vector<setting_base *> _settings;

public:
    inline setting_redirect(setting_container *dictionary, const nameset &names,
        const std::initializer_list<setting_base *> &settings, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description), _settings(settings)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        // this is a bit ugly, but we run the parse function for
        // every setting that we redirect from. for every entry
        // except the last, we'll backup & restore the state.
        for (size_t i = 0; i < _settings.size(); i++) {
            if (i != _settings.size() - 1) {
                parser.push_state();
            }

            if (!_settings[i]->parse(settingName, parser, locked)) {
                return false;
            }

            if (i != _settings.size() - 1) {
                parser.pop_state();
            }
        }

        return true;
    }

    virtual std::string stringValue() const override { return _settings[0]->stringValue(); }

    virtual std::string format() const override { return _settings[0]->format(); }
};

template<typename T>
class setting_numeric : public setting_value<T>
{
protected:
    T _min, _max;

    virtual void setValueInternal(T f, source newsource) override
    {
        if (f < _min) {
            LogPrint("WARNING: '{}': {} is less than minimum value {}.\n", primaryName(), f, _min);
            f = _min;
        }
        if (f > _max) {
            LogPrint("WARNING: '{}': {} is greater than maximum value {}.\n", primaryName(), f, _max);
            f = _max;
        }

        setting_value::setValueInternal(f, newsource);
    }

public:
    inline setting_numeric(setting_container *dictionary, const nameset &names, T v, T minval, T maxval,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description), _min(minval), _max(maxval)
    {
        // check the default value is valid
        Q_assert(_min < _max);
        Q_assert(_value >= _min);
        Q_assert(_value <= _max);
    }

    template<typename = std::enable_if_t<!std::is_enum_v<T>>>
    inline setting_numeric(setting_container *dictionary, const nameset &names, T v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_numeric(
              dictionary, names, v, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), group, description)
    {
    }

    template<typename = std::enable_if_t<!std::is_enum_v<T>>>
    inline bool boolValue() const
    {
        return _value > 0;
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        try {
            T f;

            if constexpr (std::is_floating_point_v<T>) {
                f = std::stod(parser.token);
            } else {
                f = static_cast<T>(std::stoull(parser.token));
            }

            setValueFromParse(f, locked);

            return true;
        }
        catch (std::exception &) {
            return false;
        }
    }

    virtual std::string stringValue() const override { return std::to_string(_value); }

    virtual std::string format() const override { return "n"; }
};

using setting_scalar = setting_numeric<vec_t>;
using setting_int32 = setting_numeric<int32_t>;

template<typename T>
class setting_enum : public setting_value<T>
{
private:
    std::map<std::string, T, natural_less> _values;

public:
    inline setting_enum(setting_container *dictionary, const nameset &names, T v,
        const std::initializer_list<std::pair<const char *, T>> &enumValues, const setting_group *group = nullptr,
        const char *description = "")
        : setting_value(dictionary, names, v, group, description), _values(enumValues.begin(), enumValues.end())
    {
    }

    virtual std::string stringValue() const override
    {
        for (auto &value : _values) {
            if (value.second == _value) {
                return value.first;
            }
        }

        throw std::exception();
    }

    virtual std::string format() const override
    {
        std::string f;

        for (auto &value : _values) {
            if (!f.empty()) {
                f += " | ";
            }

            f += value.first;
        }

        return f;
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        if (auto it = _values.find(parser.token); it != _values.end()) {
            setValueFromParse(it->second, locked);
            return true;
        }

        return false;
    }
};

class setting_string : public setting_value<std::string>
{
private:
    std::string _format;

public:
    inline setting_string(setting_container *dictionary, const nameset &names, std::string v,
        const std::string_view &format = "\"str\"", const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, v, group, description), _format(format)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        if (auto value = parseString(parser)) {
            setValueFromParse(std::move(*value), locked);
            return true;
        }

        return false;
    }

    [[deprecated("use value()")]] virtual std::string stringValue() const override { return _value; }

    virtual std::string format() const override { return _format; }
};

class setting_vec3 : public setting_value<qvec3d>
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const { return val; }

    virtual void setValueInternal(qvec3d f, source newsource) override
    {
        setting_value::setValueInternal(transformVec3Value(f), newsource);
    }

public:
    inline setting_vec3(setting_container *dictionary, const nameset &names, vec_t a, vec_t b, vec_t c,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value(dictionary, names, transformVec3Value({a, b, c}), group, description)
    {
    }

    virtual bool parse(const std::string &settingName, parser_base_t &parser, bool locked = false) override
    {
        qvec3d vec;

        for (int i = 0; i < 3; i++) {
            if (!parser.parse_token()) {
                return false;
            }

            try {
                vec[i] = std::stod(parser.token);
            }
            catch (std::exception &) {
                return false;
            }
        }

        setValueFromParse(vec, locked);

        return true;
    }

    virtual std::string stringValue() const override { return qv::to_string(_value); }

    virtual std::string format() const override { return "x y z"; }
};

class setting_mangle : public setting_vec3
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const override { return qv::vec_from_mangle(val); }

public:
    using setting_vec3::setting_vec3;
};

class setting_color : public setting_vec3
{
protected:
    virtual qvec3d transformVec3Value(const qvec3d &val) const override { return qv::normalize_color_format(val); }

public:
    using setting_vec3::setting_vec3;
};

// settings dictionary

class setting_container
{
    struct less
    {
        constexpr bool operator()(const setting_group *a, const setting_group *b) const
        {
            int32_t a_order = a ? a->order : std::numeric_limits<int32_t>::min();
            int32_t b_order = b ? b->order : std::numeric_limits<int32_t>::min();

            return a_order < b_order;
        }
    };

    std::map<std::string, setting_base *> _settingsmap;
    std::set<setting_base *> _settings;
    std::map<const setting_group *, std::set<setting_base *>, less> _groupedSettings;

public:
    std::string programName, remainderName = "filename", usage;

    inline void registerSetting(setting_base *setting)
    {
        for (const auto &name : setting->names()) {
            Q_assert(_settingsmap.find(name) == _settingsmap.end());
            _settingsmap.emplace(name, setting);
        }

        _settings.emplace(setting);
        _groupedSettings[setting->getGroup()].insert(setting);
    }

    template<typename TIt>
    inline void registerSettings(TIt begin, TIt end)
    {
        for (auto it = begin; it != end; it++) {
            registerSetting(*it);
        }
    }

    inline void registerSettings(const std::initializer_list<setting_base *> &settings)
    {
        registerSettings(settings.begin(), settings.end());
    }

    inline setting_base *findSetting(const std::string &name) const
    {
        // strip off leading underscores
        if (name.find("_") == 0) {
            return findSetting(name.substr(1, name.size() - 1));
        }

        if (auto it = _settingsmap.find(name); it != _settingsmap.end()) {
            return it->second;
        }

        return nullptr;
    }

    inline void setSetting(const std::string &name, const std::string &value, bool locked)
    {
        setting_base *setting = findSetting(name);

        if (setting == nullptr) {
            if (locked) {
                throw parse_exception(fmt::format("Unrecognized command-line option '{}'\n", name));
            }
            return;
        }

        setting->parse(name, parser_t{value}, locked);
    }

    inline void setSettings(const entdict_t &epairs, bool locked)
    {
        for (const auto &epair : epairs) {
            setSetting(epair.first, epair.second, locked);
        }
    }

    inline auto begin() { return _settings.begin(); }
    inline auto end() { return _settings.end(); }

    inline auto begin() const { return _settings.begin(); }
    inline auto end() const { return _settings.end(); }

    inline auto grouped() const { return _groupedSettings; }

    [[noreturn]] void printHelp();
    void printSummary();

    /**
     * Parse options from the input parser. The parsing
     * process is fairly tolerant, and will only really
     * fail hard if absolutely necessary. The remainder
     * of the command line is returned (anything not
     * eaten by the options).
     */
    std::vector<std::string> parse(parser_base_t &parser);
};

// global groups
extern setting_group performance_group, logging_group;

class common_settings : public virtual setting_container
{
public:
    // global settings
    setting_int32 threads{
        this, "threads", 0, &performance_group, "number of threads to use, maximum; leave 0 for automatic"};

    setting_bool verbose{this, {"verbose", "v"}, false, &logging_group, "verbose output"};
    setting_bool quiet{this, {"quiet", "noverbose"}, false, &logging_group, "suppress non-important output"};
    setting_bool nopercent{this, "nopercent", false, &logging_group, "don't output percentage messages"};

    virtual void setParameters(int argc, const char **argv);

    // before the parsing routine; set up options, members, etc
    virtual void preinitialize(int argc, const char **argv) { setParameters(argc, argv); }
    // do the actual parsing
    virtual void initialize(int argc, const char **argv) { parse(token_parser_t(argc, argv)); }
    // after parsing has concluded, handle the side effects
    virtual void postinitialize(int argc, const char **argv);

    // run all three steps
    inline void run(int argc, const char **argv)
    {
        preinitialize(argc, argv);
        initialize(argc, argv);
        postinitialize(argc, argv);
    }
};
}; // namespace settings