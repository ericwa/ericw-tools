/*  Copyright (C) 1996-1997  Id Software, Inc.

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

#include <iosfwd>

#include <common/settings.hh>

// special type of setting that combines multiple
// settings. just for the casting capability of bsputil_settings.
class setting_combined : public settings::setting_base
{
protected:
    std::vector<std::shared_ptr<settings::setting_base>> _settings;

public:
    setting_combined(settings::setting_container *dictionary, const settings::nameset &names,
        std::initializer_list<std::shared_ptr<settings::setting_base>> settings,
        const settings::setting_group *group = nullptr, const char *description = "")
        : setting_base(dictionary, names, group, description),
          _settings(settings)
    {
    }
    bool copy_from(const setting_base &other) override { throw std::runtime_error("not implemented"); }
    void reset() override { throw std::runtime_error("not implemented"); }
    bool parse(const std::string &setting_name, parser_base_t &parser, settings::source source) override
    {
        throw std::runtime_error("not implemented");
    }
    std::string string_value() const override { throw std::runtime_error("not implemented"); }
    std::string format() const override { throw std::runtime_error("not implemented"); }

    template<typename TSetting>
    const TSetting *get(size_t index) const
    {
        return dynamic_cast<const TSetting *>(_settings[index].get());
    }
};

struct bsputil_settings : public settings::common_settings
{
private:
    template<typename TSetting, typename... TArgs>
    bool load_setting(
        const std::string &name, parser_base_t &parser, settings::source src, TArgs &&...setting_arguments)
    {
        auto setting = std::make_unique<TSetting>(nullptr, name, std::forward<TArgs>(setting_arguments)...);
        bool parsed = setting->parse(name, parser, src);
        operations.push_back(std::move(setting));
        return parsed;
    }

    bool load_setting(const std::string &name, settings::source src);

public:
    settings::setting_func scale;
    settings::setting_func replace_entities;
    settings::setting_func extract_entities;
    settings::setting_func extract_textures;
    settings::setting_func replace_textures;
    settings::setting_func convert;
    settings::setting_func check;
    settings::setting_func modelinfo;
    settings::setting_func findfaces;
    settings::setting_func findleaf;
    settings::setting_func settexinfo;
    settings::setting_func decompile;
    settings::setting_func decompile_geomonly;
    settings::setting_func decompile_ignore_brushes;
    settings::setting_func decompile_hull;
    settings::setting_func extract_bspx_lump;
    settings::setting_func insert_bspx_lump;
    settings::setting_func remove_bspx_lump;
    settings::setting_func svg;

    std::vector<std::unique_ptr<settings::setting_base>> operations;

    bsputil_settings();
};

struct mbsp_t;

void ExportWad(std::ofstream &wadfile, const mbsp_t *bsp);
int bsputil_main(int argc, const char **argv);
