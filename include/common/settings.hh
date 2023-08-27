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
#include <common/cmdlib.hh>

#include <vector>
#include <string>
#include <cassert>
#include <sstream>
#include <map>
#include <set>
#include <limits>
#include <optional>
#include <unordered_set>
#include <functional>

namespace settings
{
struct parse_exception : public std::exception
{
private:
    std::string _what;

public:
    parse_exception(std::string str);
    const char *what() const noexcept override;
};

// thrown after displaying `--help` text.
// the command-line tools should catch this and exit with status 0.
// tests should let the test framework catch this and fail.
// (previously, the `--help` code called exit(0); directly which caused
// spurious test successes.)
struct quit_after_help_exception : public std::exception
{
};

enum class source
{
    DEFAULT,
    GAME_TARGET,
    MAP,
    COMMANDLINE
};

class nameset : public std::vector<std::string>
{
public:
    nameset(const char *str);
    nameset(const std::string &str);
    nameset(const std::initializer_list<const char *> &strs);
    nameset(const std::initializer_list<std::string> &strs);
};

enum class expected_source
{
    commandline,
    worldspawn
};

struct setting_group
{
    const char *name;
    const int32_t order;
    expected_source type;
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

    bool change_source(source new_source);

public:
    ~setting_base() = default;

    // copy constructor is deleted. the trick we use with:
    //
    // class some_settings public settings_container {
    //     setting_bool s {this, "s", false};
    // }
    //
    // is incompatible with the settings_container/setting_base types being copyable.
    setting_base(const setting_base &other) = delete;

    // copy assignment
    setting_base &operator=(const setting_base &other) = delete;

    inline const std::string &primary_name() const { return _names.at(0); }
    inline const nameset &names() const { return _names; }
    inline const setting_group *group() const { return _group; }
    inline const char *description() const { return _description; }

    constexpr bool is_changed() const { return _source != source::DEFAULT; }
    constexpr source get_source() const { return _source; }

    const char *sourceString() const;

    // copies value and source
    virtual bool copy_from(const setting_base &other) = 0;

    // resets value to default, and source to source::DEFAULT
    virtual void reset() = 0;
    virtual bool parse(const std::string &setting_name, parser_base_t &parser, source source) = 0;
    virtual std::string string_value() const = 0;
    virtual std::string format() const = 0;
};

// a special type of setting that acts as a flag but
// calls back to a function to actually do the tasks.
// be careful because this won't show up in summary.
class setting_func : public setting_base
{
protected:
    std::function<void(source)> _func;

public:
    setting_func(setting_container *dictionary, const nameset &names, std::function<void(source)> func,
        const setting_group *group = nullptr, const char *description = "");
    bool copy_from(const setting_base &other) override;
    void reset() override;
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

// base class for a setting that has its own value
template<typename T>
class setting_value : public setting_base
{
protected:
    T _default;
    T _value;

public:
    inline setting_value(setting_container *dictionary, const nameset &names, T v, const setting_group *group = nullptr,
        const char *description = "")
        : setting_base(dictionary, names, group, description),
          _default(v),
          _value(v)
    {
    }

    const T &value() const { return _value; }

    const T &default_value() const { return _default; }

    virtual void set_value(const T &value, source new_source)
    {
        if (change_source(new_source)) {
            _value = value;
        }
    }

    inline bool copy_from(const setting_base &other) override
    {
        if (auto *casted = dynamic_cast<const setting_value<T> *>(&other)) {
            _value = casted->_value;
            _source = casted->_source;
            return true;
        }
        return false;
    }

    inline void reset() override
    {
        _value = _default;
        _source = source::DEFAULT;
    }
};

class setting_bool : public setting_value<bool>
{
protected:
    bool parseInternal(parser_base_t &parser, source source, bool truthValue);

public:
    setting_bool(setting_container *dictionary, const nameset &names, bool v, const setting_group *group = nullptr,
        const char *description = "");

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

// an extension to setting_bool; this automatically adds "no" versions
// to the list, and will allow them to be used to act as `-name 0`.
class setting_invertible_bool : public setting_bool
{
public:
    setting_invertible_bool(setting_container *dictionary, const nameset &names, bool v,
        const setting_group *group = nullptr, const char *description = "");

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
};

class setting_redirect : public setting_base
{
private:
    std::vector<setting_base *> _settings;

public:
    setting_redirect(setting_container *dictionary, const nameset &names,
        const std::initializer_list<setting_base *> &settings, const setting_group *group = nullptr,
        const char *description = "");
    bool copy_from(const setting_base &other) override;
    void reset() override;
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

template<typename T>
class setting_numeric : public setting_value<T>
{
    static_assert(!std::is_enum_v<T>, "use setting_enum for enums");

protected:
    T _min, _max;

public:
    inline setting_numeric(setting_container *dictionary, const nameset &names, T v, T minval, T maxval,
        const setting_group *group = nullptr, const char *description = "")
        : setting_value<T>(dictionary, names, v, group, description),
          _min(minval),
          _max(maxval)
    {
        // check the default value is valid
        Q_assert(_min < _max);
        Q_assert(this->_value >= _min);
        Q_assert(this->_value <= _max);
    }

    inline setting_numeric(setting_container *dictionary, const nameset &names, T v,
        const setting_group *group = nullptr, const char *description = "")
        : setting_numeric(
              dictionary, names, v, std::numeric_limits<T>::lowest(), std::numeric_limits<T>::max(), group, description)
    {
    }

    void set_value(const T &f, source new_source) override
    {
        if (f < _min) {
            logging::print("WARNING: '{}': {} is less than minimum value {}.\n", this->primary_name(), f, _min);
        }
        if (f > _max) {
            logging::print("WARNING: '{}': {} is greater than maximum value {}.\n", this->primary_name(), f, _max);
        }

        this->setting_value<T>::set_value(std::clamp(f, _min, _max), new_source);
    }

    inline bool boolValue() const { return this->_value > 0; }

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override
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

            this->set_value(f, source);

            return true;
        } catch (std::exception &) {
            return false;
        }
    }

    std::string string_value() const override { return std::to_string(this->_value); }

    std::string format() const override { return "n"; }
};

using setting_scalar = setting_numeric<vec_t>;
using setting_int32 = setting_numeric<int32_t>;

template<typename T>
class setting_enum : public setting_value<T>
{
private:
    std::map<std::string, T, natural_case_insensitive_less> _values;

public:
    inline setting_enum(setting_container *dictionary, const nameset &names, T v,
        const std::initializer_list<std::pair<const char *, T>> &enum_values, const setting_group *group = nullptr,
        const char *description = "")
        : setting_value<T>(dictionary, names, v, group, description),
          _values(enum_values.begin(), enum_values.end())
    {
    }

    std::string string_value() const override
    {
        for (auto &value : _values) {
            if (value.second == this->_value) {
                return value.first;
            }
        }

        throw std::exception();
    }

    std::string format() const override
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

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override
    {
        if (!parser.parse_token()) {
            return false;
        }

        // see if it's a string enum case label
        if (auto it = _values.find(parser.token); it != _values.end()) {
            this->set_value(it->second, source);
            return true;
        }

        // see if it's an integer
        try {
            const int i = std::stoi(parser.token);

            this->set_value(static_cast<T>(i), source);
            return true;
        } catch (std::invalid_argument &) {
        } catch (std::out_of_range &) {
        }

        return false;
    }
};

class setting_string : public setting_value<std::string>
{
private:
    std::string _format;

public:
    setting_string(setting_container *dictionary, const nameset &names, std::string v,
        const std::string_view &format = "\"str\"", const setting_group *group = nullptr, const char *description = "");
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    [[deprecated("use value()")]] std::string string_value() const override;
    std::string format() const override;
};

class setting_path : public setting_value<fs::path>
{
public:
    setting_path(setting_container *dictionary, const nameset &names, fs::path v, const setting_group *group = nullptr,
        const char *description = "");
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

class setting_set : public setting_base
{
private:
    std::unordered_set<std::string> _values;
    std::string _format;

public:
    setting_set(setting_container *dictionary, const nameset &names,
        const std::string_view &format = "\"str\" <multiple allowed>", const setting_group *group = nullptr,
        const char *description = "");

    const std::unordered_set<std::string> &values() const;

    virtual void add_value(const std::string &value, source new_source);
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    bool copy_from(const setting_base &other) override;
    void reset() override;
    std::string format() const override;
    std::string string_value() const override;
};

class setting_vec3 : public setting_value<qvec3d>
{
protected:
    virtual qvec3d transform_vec3_value(const qvec3d &val) const;

public:
    setting_vec3(setting_container *dictionary, const nameset &names, vec_t a, vec_t b, vec_t c,
        const setting_group *group = nullptr, const char *description = "");

    void set_value(const qvec3d &f, source new_source) override;
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
    std::string string_value() const override;
    std::string format() const override;
};

class setting_mangle : public setting_vec3
{
protected:
    qvec3d transform_vec3_value(const qvec3d &val) const override;

public:
    using setting_vec3::setting_vec3;

    // allow mangle to only specify pitch, or pitch yaw
    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override;
};

class setting_color : public setting_vec3
{
protected:
    qvec3d transform_vec3_value(const qvec3d &val) const override;

public:
    using setting_vec3::setting_vec3;
};

// a simple wrapper type that allows you to provide
// extra validation to an existing type without needing
// to create a whole new type for it.
template<typename T>
class setting_validator : public T
{
protected:
    std::function<bool(T &)> _validator;

public:
    template<typename... Args>
    inline setting_validator(const decltype(_validator) &validator, Args &&...args)
        : T(std::forward<Args>(args)...),
          _validator(validator)
    {
    }

    bool parse(const std::string &setting_name, parser_base_t &parser, source source) override
    {
        bool result = this->T::parse(setting_name, parser, source);

        if (result) {
            return _validator(*this);
        }

        return result;
    }
};

// settings dictionary
enum class setting_error
{
    NONE,
    MISSING,
    INVALID
};

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
    std::map<const setting_group *, std::set<setting_base *>, less> _grouped_settings;

public:
    std::string program_name;
    std::string remainder_name = "filename";
    std::string program_description;

    inline setting_container() { }

    virtual ~setting_container();

    // copy constructor (can't be copyable, see setting_base)
    setting_container(const setting_container &other) = delete;

    // copy assignment
    setting_container &operator=(const setting_container &other) = delete;

    virtual void reset();

    void copy_from(const setting_container &other);

    void register_setting(setting_base *setting);

    template<typename TIt>
    inline void register_settings(TIt begin, TIt end)
    {
        for (auto it = begin; it != end; it++) {
            register_setting(*it);
        }
    }

    void register_settings(const std::initializer_list<setting_base *> &settings);
    setting_base *find_setting(const std::string &name) const;
    setting_error set_setting(const std::string &name, const std::string &value, source source);
    void set_settings(const entdict_t &epairs, source source);

    inline auto begin() { return _settings.begin(); }
    inline auto end() { return _settings.end(); }

    inline auto begin() const { return _settings.begin(); }
    inline auto end() const { return _settings.end(); }

    inline auto grouped() const { return _grouped_settings; }

    void print_help();
    void print_summary();
    void print_rst_documentation();

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
extern setting_group performance_group, logging_group, game_group;

enum class search_priority_t
{
    LOOSE,
    ARCHIVE
};

class common_settings : public virtual setting_container
{
public:
    // global settings
    setting_int32 threads;
    setting_bool lowpriority;

    setting_invertible_bool log;
    setting_bool verbose;
    setting_bool nopercent;
    setting_bool nostat;
    setting_bool noprogress;
    setting_bool nocolor;
    setting_redirect quiet;
    setting_path gamedir;
    setting_path basedir;
    setting_enum<search_priority_t> filepriority;
    setting_set paths;
    setting_bool q2rtx;
    setting_invertible_bool defaultpaths;
    setting_scalar tex_saturation_boost;

    common_settings();

    virtual void set_parameters(int argc, const char **argv);

    // before the parsing routine; set up options, members, etc
    virtual void preinitialize(int argc, const char **argv);
    // do the actual parsing
    virtual void initialize(int argc, const char **argv);
    // after parsing has concluded, handle the side effects
    virtual void postinitialize(int argc, const char **argv);

    // run all three steps
    void run(int argc, const char **argv);
};
}; // namespace settings