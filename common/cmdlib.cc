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

#include <common/cmdlib.hh>
#include <common/log.hh>
#include <common/threads.hh>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef WIN32
#include <direct.h>
#include <windows.h>
#endif

#ifdef LINUX
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#endif

#include <cstdint>

#include <string>

constexpr char PATHSEPERATOR = '/';

/* set these before calling CheckParm */
int myargc;
char **myargv;

char com_token[1024];
bool com_eof;

/*
 * =================
 * Error
 * For abnormal program terminations
 * =================
 */
[[noreturn]] void Error(const char *error)
{
    /* Using lockless prints so we can error out while holding the lock */
    InterruptThreadProgress__();
    LogPrintLocked("************ ERROR ************\n{}\n", error);
    exit(1);
}

void // mxd
string_replaceall(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

bool // mxd
string_iequals(const std::string &a, const std::string &b)
{
    size_t sz = a.size();
    if (b.size() != sz)
        return false;
    for (size_t i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

std::filesystem::path qdir, gamedir, basedir;

/** It's possible to compile quake 1/hexen 2 maps without a qdir */
inline void ClearQdir()
{
    qdir.clear();
    gamedir.clear();
    basedir.clear();
}

constexpr const char *MAPS_FOLDER = "maps";

// mxd. Expects the path to contain "maps" folder
void SetQdirFromPath(const std::string &basedirname, std::filesystem::path path)
{
    // expand canonicals, and fetch parent of source file
    // (maps/source.map -> C:/Quake/ID1/maps/)
    path = std::filesystem::canonical(path).parent_path();

    // make sure we're maps/
    if (path.filename() != "maps") {
        FLogPrint("WARNING: '{}' is not directly inside '{}'\n", path, MAPS_FOLDER);
        return;
    }

    // set gamedir (it should be "above" the source)
    // (C:/Quake/ID1/maps/ -> C:/Quake/ID1/)
    gamedir = path.parent_path();
    LogPrint("INFO: gamedir: '{}'\n", gamedir);

    // set qdir (it should be above gamedir)
    // (C:/Quake/ID1/ -> C:/Quake/)
    qdir = gamedir.parent_path();
    LogPrint("INFO: qdir: '{}'\n", qdir);

    // Set base dir and make sure it exists
    basedir = qdir / basedirname;

    if (!std::filesystem::exists(basedir)) {
        FLogPrint("WARNING: failed to find '{}' in '{}'\n", basedirname, qdir);
        ClearQdir();
        return;
    }
}

qfile_t SafeOpenWrite(const std::filesystem::path &filename)
{
    FILE *f;

#ifdef _WIN32
    f = _wfopen(filename.c_str(), L"wb");
#else
    f = fopen(filename.string().c_str(), "wb");
#endif

    if (!f)
        FError("Error opening {}: {}", filename, strerror(errno));

    return { f, fclose };
}

qfile_t SafeOpenRead(const std::filesystem::path &filename, bool must_exist)
{
    FILE *f;

#ifdef _WIN32
    f = _wfopen(filename.c_str(), L"rb");
#else
    f = fopen(filename.string().c_str(), "rb");
#endif

    if (!f)
    {
        if (must_exist)
            FError("Error opening {}: {}", filename, strerror(errno));

        return { nullptr, nullptr };
    }

    return { f, fclose };
}

size_t SafeRead(const qfile_t &f, void *buffer, size_t count)
{
    if (fread(buffer, 1, count, f.get()) != (size_t)count)
        FError("File read failure");

    return count;
}

size_t SafeWrite(const qfile_t &f, const void *buffer, size_t count)
{
    if (fwrite(buffer, 1, count, f.get()) != (size_t)count)
        FError("File write failure");

    return count;
}

void SafeSeek(const qfile_t &f, long offset, int32_t origin)
{
    fseek(f.get(), offset, origin);
}

long SafeTell(const qfile_t &f)
{
    return ftell(f.get());
}

struct pakheader_t
{
    char magic[4];
    unsigned int tableofs;
    unsigned int numfiles;
};

struct pakfile_t
{
    char name[56];
    unsigned int offset;
    unsigned int length;
};

/*
 * ==============
 * LoadFilePak
 * reads a file directly out of a pak, to make re-lighting friendlier
 * writes to the filename, stripping the pak part of the name
 * ==============
 */
long LoadFilePak(std::filesystem::path &filename, void *destptr)
{
    // check if we have a .pak file in this path
    for (auto p = filename.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (p.extension() == ".pak") {
            qfile_t file = SafeOpenRead(p);

            // false positive
            if (!file)
                continue;

            // got one; calculate the relative remaining path
            auto innerfile = filename.lexically_relative(p);

            uint8_t **bufferptr = static_cast<uint8_t **>(destptr);
            pakheader_t header;
            long length = -1;
            SafeRead(file, &header, sizeof(header));

            header.numfiles = LittleLong(header.numfiles) / sizeof(pakfile_t);
            header.tableofs = LittleLong(header.tableofs);

            if (!strncmp(header.magic, "PACK", 4)) {
                pakfile_t *files = new pakfile_t[header.numfiles];

                SafeSeek(file, header.tableofs, SEEK_SET);
                SafeRead(file, files, header.numfiles * sizeof(*files));

                for (uint32_t i = 0; i < header.numfiles; i++) {
                    if (innerfile == files[i].name) {
                        SafeSeek(file, files[i].offset, SEEK_SET);
                        *bufferptr = new uint8_t[files[i].length + 1];
                        SafeRead(file, *bufferptr, files[i].length);
                        length = files[i].length;
                        break;
                    }
                }
                delete[] files;
            }

            if (length < 0)
                FError("Unable to find '{}' inside '{}'", innerfile, filename);

            filename = innerfile;
            return length;
        }
    }

    // not in a pak, so load it normally
    return LoadFile(filename, destptr);
}

/*
 * ===========
 * filelength
 * ===========
 */
static long Sys_FileLength(const qfile_t &f)
{
    long pos = ftell(f.get());
    fseek(f.get(), 0, SEEK_END);
    long end = ftell(f.get());
    fseek(f.get(), pos, SEEK_SET);

    return end;
}
/*
 * ==============
 * LoadFile
 * ==============
 */
long LoadFile(const std::filesystem::path &filename, void *destptr)
{
    uint8_t **bufferptr = static_cast<uint8_t **>(destptr);

    qfile_t file = SafeOpenRead(filename);

    long length = Sys_FileLength(file);

    uint8_t * buffer = *bufferptr = new uint8_t[length + 1];

    if (!buffer)
        FError("allocation of {} bytes failed.", length);

    SafeRead(file, buffer, length);

    buffer[length] = 0;

    return length;
}
