/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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

#include <filesystem>
#include <optional>
#include <vector>

namespace fs
{
using namespace std::filesystem;

using data = std::optional<std::vector<uint8_t>>;

struct archive_like
{
    path pathname;

    bool external;

    inline archive_like(const path &pathname, bool external)
        : pathname(pathname),
          external(external)
    {
    }
    virtual ~archive_like() { }

    virtual bool contains(const path &filename) = 0;

    virtual data load(const path &filename) = 0;
};

// clear all initialized/loaded data from fs
void clear();

// add the specified archive to the search path. must be the full
// path to the archive. Archives can be directories or archive-like
// files. Returns the archive if it already exists, the new
// archive added if one was added, or nullptr on error.
// `external` is a stored hint as to if the caller should consider
// the actual texture data embeddable.
std::shared_ptr<archive_like> addArchive(const path &p, bool external = false);

struct resolve_result
{
    std::shared_ptr<archive_like> archive;
    path filename;

    inline explicit operator bool() const { return (bool)archive; }
};

// attempt to resolve the specified file.
// this will attempt resolves in the following order
// given the path "maps/start.map":
// - absolute path match (ie, "maps/start.map")
// - relative path match to current working dir (ie, "c:/eric-tools/bin/maps/start.map")
// - direct archive load (only used if path has a valid archive string in it; ie, "c:/quake/id1/pak0.pak/start.map")
// - registered directories in reverse order (ie, "c:/quake/mod/maps/start.map", "c:/quake/id1/maps/start.map")
// - registered archives in reverse order (ie, "c:/quake/pak1/maps/start.map", "c:/quake/pak0/maps/start.map")
// returns the archive that it is contained in, and the filename.
// the filename is only different from p if p is an archive path.
resolve_result where(const path &p, bool prefer_loose = false);

// attempt to load the specified resolve result.
data load(const resolve_result &pos);

// attempt to load the specified file from the specified path.
// shortcut to load(where(p))
data load(const path &p, bool prefer_loose = false);

struct archive_components
{
    path archive, filename;

    inline operator bool() { return archive.has_relative_path(); }
};

// Splits an archive load path (ie, "C:/pak0.pak/file/path") into two components ("C:/pak0.pak", "file/path").
archive_components splitArchivePath(const path &source);

// Quick helper to get the path this file would be in
// if it wasn't in a pak
path resolveArchivePath(const path &source);
}; // namespace fs

// Returns the path itself if it has an extension already, otherwise
// returns the path with extension replaced with `extension`.
fs::path DefaultExtension(const fs::path &path, const fs::path &extension);

#include <fmt/core.h>

// TODO: no wchar_t support in this version apparently
template<>
struct fmt::formatter<fs::path>
{
    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template<typename FormatContext>
    auto format(const fs::path &p, FormatContext &ctx)
    {
        return fmt::format_to(ctx.out(), "{}", p.string());
    }
};
