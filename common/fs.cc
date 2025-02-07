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

#include "common/cmdlib.hh"
#include "common/fs.hh"
#include "common/log.hh"
#include <fstream>
#include <memory>
#include <array>
#include <list>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace fs
{
struct directory_archive : archive_like
{
    using archive_like::archive_like;

    bool contains(const path &filename) override
    {
        return exists(!pathname.empty() ? (pathname / filename) : filename);
    }

    data load(const path &filename) override
    {
        path p = !pathname.empty() ? (pathname / filename) : filename;

        if (!exists(p)) {
            return std::nullopt;
        }

        try {
            uintmax_t size = file_size(p);
            std::ifstream stream(p, std::ios_base::in | std::ios_base::binary);
            std::vector<uint8_t> data(size);
            stream.read(reinterpret_cast<char *>(data.data()), size);
            return data;
        } catch (const filesystem_error &e) {
            logging::funcprint("WARNING: {}\n", e.what());
            return std::nullopt;
        }
    }
};

struct pak_archive : archive_like
{
    std::ifstream pakstream;

    struct pak_header
    {
        std::array<char, 4> magic;
        uint32_t offset;
        uint32_t size;

        auto stream_data() { return std::tie(magic, offset, size); }
    };

    struct pak_file
    {
        std::array<char, 56> name;
        uint32_t offset;
        uint32_t size;

        auto stream_data() { return std::tie(name, offset, size); }
    };

    std::unordered_map<std::string, std::tuple<uint32_t, uint32_t>, case_insensitive_hash, case_insensitive_equal>
        files;

    inline pak_archive(const path &pathname, bool external)
        : archive_like(pathname, external),
          pakstream(pathname, std::ios_base::in | std::ios_base::binary)
    {
        pakstream >> endianness<std::endian::little>;

        pak_header header;

        pakstream >= header;

        if (header.magic != std::array<char, 4>{'P', 'A', 'C', 'K'}) {
            throw std::runtime_error("Bad magic");
        }

        size_t totalFiles = header.size / sizeof(pak_file);

        files.reserve(totalFiles);

        pakstream.seekg(header.offset);

        for (size_t i = 0; i < totalFiles; i++) {
            pak_file file;

            pakstream >= file;

            files[file.name.data()] = std::make_tuple(file.offset, file.size);
        }
    }

    bool contains(const path &filename) override { return files.find(filename.generic_string()) != files.end(); }

    data load(const path &filename) override
    {
        auto it = files.find(filename.generic_string());

        if (it == files.end()) {
            return std::nullopt;
        }

        pakstream.seekg(std::get<0>(it->second));
        uintmax_t size = std::get<1>(it->second);
        std::vector<uint8_t> data(size);
        pakstream.read(reinterpret_cast<char *>(data.data()), size);
        return data;
    }
};

struct wad_archive : archive_like
{
    std::ifstream wadstream;

    // WAD Format
    struct wad_header
    {
        std::array<char, 4> identification;
        uint32_t numlumps;
        uint32_t infotableofs;

        auto stream_data() { return std::tie(identification, numlumps, infotableofs); }
    };

    static constexpr std::array<char, 4> wad2_ident = {'W', 'A', 'D', '2'};
    static constexpr std::array<char, 4> wad3_ident = {'W', 'A', 'D', '3'};

    struct wad_lump_header
    {
        uint32_t filepos;
        uint32_t disksize;
        uint32_t size; // uncompressed
        uint8_t type;
        uint8_t compression;
        padding<2> pad;
        std::array<char, 16> name; // must be null terminated
                                   // NOTE: textures using all 16 exist in the wild, e.g. openquartzmirror
                                   // in free_wad.wad

        auto stream_data() { return std::tie(filepos, disksize, size, type, compression, pad, name); }

        std::string name_as_string() const
        {
            size_t length = 0;

            // count the number of leading non-null characters
            for (int i = 0; i < 16; ++i) {
                if (name[i] != 0)
                    ++length;
                else
                    break;
            }

            return std::string(name.data(), length);
        }
    };

    std::unordered_map<std::string, std::tuple<uint32_t, uint32_t>, case_insensitive_hash, case_insensitive_equal>
        files;

    inline wad_archive(const path &pathname, bool external)
        : archive_like(pathname, external),
          wadstream(pathname, std::ios_base::in | std::ios_base::binary)
    {
        wadstream >> endianness<std::endian::little>;

        wad_header header;

        wadstream >= header;

        if (header.identification != wad2_ident && header.identification != wad3_ident) {
            throw std::runtime_error("Bad magic");
        }

        files.reserve(header.numlumps);

        wadstream.seekg(header.infotableofs);

        for (size_t i = 0; i < header.numlumps; i++) {
            wad_lump_header file;

            wadstream >= file;

            std::string tex_name = file.name_as_string();
            if (tex_name.size() == 16) {
                logging::print("WARNING: texture name {} ({}) is not null-terminated\n", tex_name, pathname);
            }
            files[tex_name] = std::make_tuple(file.filepos, file.disksize);
        }
    }

    bool contains(const path &filename) override { return files.find(filename.generic_string()) != files.end(); }

    data load(const path &filename) override
    {
        auto it = files.find(filename.generic_string());

        if (it == files.end()) {
            return std::nullopt;
        }

        wadstream.seekg(std::get<0>(it->second));
        uintmax_t size = std::get<1>(it->second);
        std::vector<uint8_t> data(size);
        wadstream.read(reinterpret_cast<char *>(data.data()), size);
        return data;
    }
};

static std::shared_ptr<directory_archive> absrel_dir = std::make_shared<directory_archive>("", false);
std::list<std::shared_ptr<archive_like>> archives, directories;

/** It's possible to compile quake 1/hexen 2 maps without a qdir */
void clear()
{
    archives.clear();
    directories.clear();
}

inline std::shared_ptr<archive_like> addArchiveInternal(const path &p, bool external)
{
    if (is_directory(p)) {
        for (auto &dir : directories) {
            std::error_code ec;
            if (equivalent(dir->pathname, p, ec)) {
                return dir;
            }
        }

        auto &arch = directories.emplace_front(std::make_shared<directory_archive>(p, external));
        logging::print(logging::flag::VERBOSE, "Added directory '{}'\n", p);
        return arch;
    } else {
        for (auto &arch : archives) {
            std::error_code ec;
            if (equivalent(arch->pathname, p, ec)) {
                return arch;
            }
        }

        auto ext = p.extension();

        try {
            if (string_iequals(ext.generic_string(), ".pak")) {
                auto &arch = archives.emplace_front(std::make_shared<pak_archive>(p, external));
                auto &pak = reinterpret_cast<std::shared_ptr<pak_archive> &>(arch);
                logging::print(logging::flag::VERBOSE, "Added pak '{}' with {} files\n", p, pak->files.size());
                return arch;
            } else if (string_iequals(ext.generic_string(), ".wad")) {
                auto &arch = archives.emplace_front(std::make_shared<wad_archive>(p, external));
                auto &wad = reinterpret_cast<std::shared_ptr<wad_archive> &>(arch);
                logging::print(logging::flag::VERBOSE, "Added wad '{}' with {} lumps\n", p, wad->files.size());
                return arch;
            } else {
                logging::funcprint("WARNING: no idea what to do with archive '{}'\n", p);
            }
        } catch (std::exception e) {
            logging::funcprint("WARNING: unable to load archive '{}': {}\n", p, e.what());
        }
    }

    return nullptr;
}

std::shared_ptr<archive_like> addArchive(const path &p, bool external)
{
    if (p.empty()) {
        logging::funcprint("WARNING: can't add empty archive path\n");
        return nullptr;
    }

    if (!exists(p)) {
        // check relative
        path filename = p.filename();

        if (!exists(filename)) {
            logging::funcprint("WARNING: archive '{}' not found\n", p);
            return nullptr;
        }

        return addArchiveInternal(filename, external);
    }

    return addArchiveInternal(p, external);
}

resolve_result where(const path &p, bool prefer_loose)
{
    // check direct archive loading first; it can't ever
    // be loose, so there's no sense for it to be in the
    // loop below
    if (auto paths = splitArchivePath(p)) {
        if (auto arch = addArchive(paths.archive)) {
            return {arch, paths.filename};
        }
    }

    for (int32_t pass = 0; pass < 2; pass++) {
        if (prefer_loose != !!pass) {
            // check absolute + relative

            // !is_directory() is a hack to avoid picking up a dir called "light"
            // when requesting a texture called "light" (was happening on CI)
            if (exists(p) && !is_directory(p)) {
                return {absrel_dir, p};
            }
        } else if (!p.is_absolute()) { // absolute doesn't make sense for other load types
            for (int32_t archive_pass = 0; archive_pass < 2; archive_pass++) {
                // check directories & archives, depending on whether
                // we want loose first or not
                for (auto &dir : (prefer_loose != !!archive_pass) ? directories : archives) {
                    if (dir->contains(p)) {
                        return {dir, p};
                    }
                }
            }
        }
    }

    return {};
}

data load(const resolve_result &pos)
{
    if (!pos) {
        return std::nullopt;
    }

    logging::print(logging::flag::VERBOSE, "Loaded '{}' from archive '{}'\n", pos.filename, pos.archive->pathname);

    return pos.archive->load(pos.filename);
}

data load(const path &p, bool prefer_loose)
{
    return load(where(p, prefer_loose));
}

archive_components splitArchivePath(const path &source)
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

path resolveArchivePath(const path &source)
{
    if (auto paths = splitArchivePath(source)) {
        return paths.archive.parent_path() / paths.filename;
    }

    return source;
}
} // namespace fs

fs::path DefaultExtension(const fs::path &path, const fs::path &extension)
{
    if (path.has_extension())
        return path;

    return fs::path(path).replace_extension(extension);
}
