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
#include <common/mathlib.hh>

#include <vector>
#include <string>
#include <cassert>
#include <sstream>
#include <map>
#include <limits>

enum class setting_source_t
{
    DEFAULT = 0,
    MAP = 1,
    COMMANDLINE = 2
};

enum class vec3_transformer_t
{
    NONE,
    MANGLE_TO_VEC,
    NORMALIZE_COLOR_TO_255
};

/* detect colors with components in 0-1 and scale them to 0-255 */
constexpr void normalize_color_format(qvec3d &color)
{
    if (color[0] >= 0 && color[0] <= 1 && color[1] >= 0 && color[1] <= 1 && color[2] >= 0 && color[2] <= 1) {
        color *= 255;
    }
}

class lockable_setting_t
{
protected:
    setting_source_t _source;
    std::vector<std::string> _names;

    lockable_setting_t(std::vector<std::string> names) : _source(setting_source_t::DEFAULT), _names(names)
    {
        Q_assert(_names.size() > 0);
    }

    bool changeSource(setting_source_t newSource)
    {
        if (static_cast<int>(newSource) >= static_cast<int>(_source)) {
            _source = newSource;
            return true;
        }
        return false;
    }

public:
    const std::string &primaryName() const { return _names.at(0); }
    const std::vector<std::string> &names() const { return _names; }

    virtual void setStringValue(const std::string &str, bool locked = false) = 0;
    virtual std::string stringValue() const = 0;

    bool isChanged() const { return _source != setting_source_t::DEFAULT; }
    bool isLocked() const { return _source == setting_source_t::COMMANDLINE; }

    std::string sourceString() const
    {
        switch (_source) {
            case setting_source_t::DEFAULT: return "default";
            case setting_source_t::MAP: return "map";
            case setting_source_t::COMMANDLINE: return "commandline";
            default: FError("Error: unknown setting source");
        }
    }
};

class lockable_bool_t : public lockable_setting_t
{
private:
    bool _default, _value;

    void setBoolValueInternal(bool f, setting_source_t newsource)
    {
        if (changeSource(newsource)) {
            _value = f;
        }
    }

public:
    void setBoolValueLocked(bool f) { setBoolValueInternal(f, setting_source_t::COMMANDLINE); }

    void setBoolValue(bool f) { setBoolValueInternal(f, setting_source_t::MAP); }

    bool boolValue() const { return _value; }

    virtual void setStringValue(const std::string &str, bool locked = false)
    {
        int intval = std::stoi(str);

        const bool f = (intval != 0 && intval != -1); // treat 0 and -1 as false
        if (locked)
            setBoolValueLocked(f);
        else
            setBoolValue(f);
    }

    virtual std::string stringValue() const { return _value ? "1" : "0"; }

    lockable_bool_t(std::vector<std::string> names, bool v) : lockable_setting_t(names), _default(v), _value(v) { }

    lockable_bool_t(std::string name, bool v) : lockable_bool_t(std::vector<std::string>{name}, v) { }
};

class lockable_vec_t : public lockable_setting_t
{
private:
    vec_t _default, _value, _min, _max;

    inline void setFloatInternal(vec_t f, setting_source_t newsource)
    {
        if (changeSource(newsource)) {
            if (f < _min) {
                LogPrint("WARNING: '{}': {} is less than minimum value {}.\n", primaryName(), f, _min);
                f = _min;
            }
            if (f > _max) {
                LogPrint("WARNING: '{}': {} is greater than maximum value {}.\n", primaryName(), f, _max);
                f = _max;
            }
            _value = f;
        }
    }

public:
    bool boolValue() const
    {
        // we use -1 to mean false
        return intValue() == 1;
    }

    int intValue() const { return static_cast<int>(_value); }

    const vec_t &floatValue() const { return _value; }

    void setFloatValue(vec_t f) { setFloatInternal(f, setting_source_t::MAP); }

    void setFloatValueLocked(vec_t f) { setFloatInternal(f, setting_source_t::COMMANDLINE); }

    virtual void setStringValue(const std::string &str, bool locked = false)
    {
        vec_t f = 0.0;
        try {
            f = std::stod(str);
        }
        catch (std::exception &) {
            LogPrint("WARNING: couldn't parse '{}' as number for key '{}'\n", str, primaryName());
        }
        if (locked)
            setFloatValueLocked(f);
        else
            setFloatValue(f);
    }

    virtual std::string stringValue() const { return std::to_string(_value); }

    lockable_vec_t(std::vector<std::string> names, vec_t v, vec_t minval = -std::numeric_limits<vec_t>::infinity(),
        vec_t maxval = std::numeric_limits<vec_t>::infinity())
        : lockable_setting_t(names), _default(v), _value(v), _min(minval), _max(maxval)
    {
        // check the default value is valid
        Q_assert(_min < _max);
        Q_assert(_value >= _min);
        Q_assert(_value <= _max);
    }

    lockable_vec_t(std::string name, vec_t v, vec_t minval = -std::numeric_limits<vec_t>::infinity(),
        vec_t maxval = std::numeric_limits<vec_t>::infinity())
        : lockable_vec_t(std::vector<std::string>{name}, v, minval, maxval)
    {
    }
};

class lockable_string_t : public lockable_setting_t
{
private:
    std::string _default, _value;

public:
    virtual void setStringValue(const std::string &str, bool locked = false)
    {
        if (changeSource(locked ? setting_source_t::COMMANDLINE : setting_source_t::MAP)) {
            _value = str;
        }
    }

    virtual std::string stringValue() const { return _value; }

    lockable_string_t(std::vector<std::string> names, std::string v) : lockable_setting_t(names), _default(v), _value(v)
    {
    }

    lockable_string_t(std::string name, std::string v) : lockable_string_t(std::vector<std::string>{name}, v) { }
};

class lockable_vec3_t : public lockable_setting_t
{
private:
    qvec3d _default, _value;
    vec3_transformer_t _transformer;

    void transformVec3Value(const qvec3d &val, qvec3d &out) const
    {
        // apply transform
        switch (_transformer) {
            case vec3_transformer_t::NONE: VectorCopy(val, out); break;
            case vec3_transformer_t::MANGLE_TO_VEC: VectorCopy(vec_from_mangle(val), out); break;
            case vec3_transformer_t::NORMALIZE_COLOR_TO_255:
                out = val;
                normalize_color_format(out);
                break;
        }
    }

    void transformAndSetVec3Value(const qvec3d &val, setting_source_t newsource)
    {
        if (changeSource(newsource)) {
            transformVec3Value(val, _value);
        }
    }

public:
    lockable_vec3_t(
        std::vector<std::string> names, vec_t a, vec_t b, vec_t c, vec3_transformer_t t = vec3_transformer_t::NONE)
        : lockable_setting_t(names), _transformer(t)
    {
        transformVec3Value({a, b, c}, _default);
        _value = _default;
    }

    lockable_vec3_t(std::string name, vec_t a, vec_t b, vec_t c, vec3_transformer_t t = vec3_transformer_t::NONE)
        : lockable_vec3_t(std::vector<std::string>{name}, a, b, c, t)
    {
    }

    const qvec3d &vec3Value() const { return _value; }

    void setVec3Value(const qvec3d &val) { transformAndSetVec3Value(val, setting_source_t::MAP); }

    void setVec3ValueLocked(const qvec3d &val) { transformAndSetVec3Value(val, setting_source_t::COMMANDLINE); }

    virtual void setStringValue(const std::string &str, bool locked = false)
    {
        qvec3d vec{};

        if (sscanf(str.c_str(), "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3) {
            LogPrint("WARNING: Not 3 values for {}\n", primaryName());
        }

        if (locked)
            setVec3ValueLocked(vec);
        else
            setVec3Value(vec);
    }

    virtual std::string stringValue() const { return qv::to_string(_value); }
};

// settings dictionary

class settingsdict_t
{
private:
    std::map<std::string, lockable_setting_t *> _settingsmap;
    std::vector<lockable_setting_t *> _allsettings;

public:
    settingsdict_t() { }

    settingsdict_t(std::vector<lockable_setting_t *> settings) : _allsettings(settings)
    {
        for (lockable_setting_t *setting : settings) {
            Q_assert(setting->names().size() > 0);

            for (const auto &name : setting->names()) {
                Q_assert(_settingsmap.find(name) == _settingsmap.end());

                _settingsmap[name] = setting;
            }
        }
    }

    lockable_setting_t *findSetting(std::string name) const
    {
        // strip off leading underscores
        if (name.find("_") == 0) {
            return findSetting(name.substr(1, name.size() - 1));
        }

        auto it = _settingsmap.find(name);
        if (it != _settingsmap.end()) {
            return it->second;
        } else {
            return nullptr;
        }
    }

    void setSetting(std::string name, std::string value, bool cmdline)
    {
        lockable_setting_t *setting = findSetting(name);
        if (setting == nullptr) {
            if (cmdline) {
                FError("Unrecognized command-line option '{}'\n", name);
            }
            return;
        }

        setting->setStringValue(value, cmdline);
    }

    void setSettings(const entdict_t &epairs, bool cmdline)
    {
        for (const auto &epair : epairs) {
            setSetting(epair.first, epair.second, cmdline);
        }
    }

    const std::vector<lockable_setting_t *> &allSettings() const { return _allsettings; }
};
