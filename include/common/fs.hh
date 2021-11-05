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

        inline archive_like(const path &pathname) :
            pathname(pathname)
        {
        }

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

    // attempt to load the specified file.
    // this will attempt loads in the following order
    // given the path "maps/start.map":
    // - absolute path match (ie, "maps/start.map")
    // - relative path match to current working dir (ie, "c:/eric-tools/bin/maps/start.map")
    // - direct archive load (only used if path has a valid archive string in it; ie, "c:/quake/id1/pak0.pak/start.map")
    // - registered directories in reverse order (ie, "c:/quake/mod/maps/start.map", "c:/quake/id1/maps/start.map")
    // - registered archives in reverse order (ie, "c:/quake/pak1/maps/start.map", "c:/quake/pak0/maps/start.map")
    data load(const path &p);

    // Splits an archive load path (ie, "C:/pak0.pak/file/path") into two components ("C:/pak0.pak", "file/path").
    inline std::tuple<path, path> splitArchivePath(const path &source)
    {
        // check direct archive loading
        // this is a bit complex, but we check the whole
        // path to see if any piece of it that isn't
        // the last piece matches a file
        for (path archive = source.parent_path(); archive.has_relative_path(); archive = archive.parent_path()) {
            if (!is_regular_file(archive)) {
                continue;
            }

            return std::make_tuple(archive, source.lexically_relative(archive));
        }

        return {};
    }

    // Quick helper to get the path this file would be in
    // if it wasn't in a pak
    inline path resolveArchivePath(const path &source)
    {
        auto archive = splitArchivePath(source);

        if (std::get<0>(archive).has_relative_path()) {
            return std::get<0>(archive).parent_path() / std::get<1>(archive);
        }

        return source;
    }
};

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

long LoadFilePak(std::filesystem::path &filename, void *destptr);
long LoadFile(const std::filesystem::path &filename, void *destptr);