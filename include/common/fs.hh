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

namespace fs
{
using namespace std::filesystem;

using data = std::optional<std::vector<uint8_t>>;

struct archive_like
{
    path pathname;

    inline archive_like(const path &pathname) : pathname(pathname) { }

    virtual bool contains(const path &filename) = 0;

    virtual data load(const path &filename) = 0;
};

extern std::filesystem::path qdir, // c:/Quake/, c:/Hexen II/ etc.
    gamedir, // c:/Quake/mymod/
    basedir; // c:/Quake/ID1/, c:/Quake 2/BASEQ2/ etc.

// clear all initialized/loaded data from fs
void clear();

// add the specified archive to the search path. must be the full
// path to the archive. Archives can be directories or archive-like
// files. Returns the archive if it already exists, the new
// archive added if one was added, or nullptr on error.
std::shared_ptr<archive_like> addArchive(const path &p);

struct resolve_result
{
    std::shared_ptr<archive_like> archive;
    path filename;

    inline operator bool() { return (bool)archive; }
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
resolve_result where(const path &p);

// attempt to load the specified file using the
// archive found via where(p)
data load(const path &p);

struct archive_components
{
    path archive, filename;

    inline operator bool() { return archive.has_relative_path(); }
};

// Splits an archive load path (ie, "C:/pak0.pak/file/path") into two components ("C:/pak0.pak", "file/path").
inline archive_components splitArchivePath(const path &source)
{
    // check direct archive loading
    // this is a bit complex, but we check the whole
    // path to see if any piece of it that isn't
    // the last piece matches a file
    for (path archive = source.parent_path(); archive.has_relative_path(); archive = archive.parent_path()) {
        if (is_regular_file(archive)) {
            return {archive, source.lexically_relative(archive)};
        }
    }

    return {};
}

// Quick helper to get the path this file would be in
// if it wasn't in a pak
inline path resolveArchivePath(const path &source)
{
    if (auto paths = splitArchivePath(source)) {
        return paths.archive.parent_path() / paths.filename;
    }

    return source;
}
}; // namespace fs

// Returns the path itself if it has an extension already, otherwise
// returns the path with extension replaced with `extension`.
inline std::filesystem::path DefaultExtension(const std::filesystem::path &path, const std::filesystem::path &extension)
{
    if (path.has_extension())
        return path;

    return std::filesystem::path(path).replace_extension(extension);
}

using qfile_t = std::unique_ptr<FILE, decltype(&fclose)>;

qfile_t SafeOpenWrite(const std::filesystem::path &filename);
qfile_t SafeOpenRead(const std::filesystem::path &filename, bool must_exist = false);
size_t SafeRead(const qfile_t &f, void *buffer, size_t count);
size_t SafeWrite(const qfile_t &f, const void *buffer, size_t count);
void SafeSeek(const qfile_t &f, long offset, int32_t origin);
long SafeTell(const qfile_t &f);
