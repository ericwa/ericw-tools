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
#include <string.h>
#endif

#include <stdint.h>

#include <string>

#define PATHSEPERATOR '/'

/* set these before calling CheckParm */
int myargc;
char **myargv;

char com_token[1024];
qboolean com_eof;

qboolean archive;
char archivedir[1024];

/*
 * =================
 * Error
 * For abnormal program terminations
 * =================
 */
[[noreturn]] void
Error(const char *error, ...)
{
    va_list argptr;

    /* Using lockless prints so we can error out while holding the lock */
    InterruptThreadProgress__();
    logprint_locked__("************ ERROR ************\n");

    va_start(argptr, error);
    logvprint(error, argptr);
    va_end(argptr);
    logprint_locked__("\n");
    exit(1);
}


/*
 * qdir will hold the path up to the base directory (basedir), including the
 * slash:
 *      c:\quake\
 *      /usr/local/games/quake/
 *
 * gamedir will hold qdir + the game directory (id1, hipnotic, etc).
 * SetQdirFromPath requires an input containing both the basedir and the
 * gamedir:
 *      c:\quake\id1\somefile.dat
 *      /usr/local/games/quake/id1/somefile.dat
 * or similar partials:
 *      id1\somefile.dat
 *      quake/id1/somefile.dat
 *
 */

char qdir[1024];        // c:/Quake/, c:/Hexen II/ etc.
char gamedir[1024];     // c:/Quake/mymod/
char basedir[1024]; //mxd. c:/Quake/ID1/, c:/Quake 2/BASEQ2/ etc.

void //mxd
string_replaceall(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty()) return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

bool //mxd
string_iequals(const std::string& a, const std::string& b)
{
    const auto sz = a.size();
    if (b.size() != sz) return false;
    for (unsigned int i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

int //mxd. Returns offset in path to the next updir, sets dirname to the name of updir
up_dir_pos(const std::string& path, std::string& dirname)
{
    const int pos = path.rfind(PATHSEPERATOR);
    if (pos > -1) dirname = path.substr(pos + 1, path.size() - pos - 1);
    return pos;
}


int //mxd. Finds dirname in path and returns offset to it
find_dir(const std::string& path, const std::string& dirname)
{
    std::string cur_path { path };
    std::string cur_dir {};
    int pos = 0;

    while (pos != -1) {
        pos = up_dir_pos(cur_path, cur_dir);
        if (pos > -1) {
            if (string_iequals(cur_dir, dirname)) return pos;
            cur_path = cur_path.substr(0, pos);
        }
    }

    return pos;
}

bool //mxd
dir_exists(const char *path)
{
    struct stat buf{};
    return (stat(path, &buf) == 0 && buf.st_mode & S_IFDIR);
}

/**
 * It's possible to compile quake 1/hexen 2 maps without a qdir
 */
static void
ClearQdir(void)
{
    qdir[0] = '\0';
    gamedir[0] = '\0';
    basedir[0] = '\0';
}

void //mxd. Expects the path to contain "maps" folder
SetQdirFromPath(const char *basedirname, const char *path)
{
    char temp[1024];
    
    if (!(path[0] == PATHSEPERATOR || path[0] == '\\' || path[1] == ':')) {
        // path is partial
        Q_getwd(temp);
        strcat(temp, "/");
        strcat(temp, path);
        path = temp;
    }

    const std::string basedir_s { basedirname };
    std::string path_s{ path };
    string_replaceall(path_s, "\\", "/");

    int pos = find_dir(path_s, "maps");
    if (pos == -1) {
        logprint("SetQdirFromPath: no \"maps\" in '%s'", path);
        ClearQdir();
        return;
    }

    // Expect mod folder to be above "maps" folder
    path_s = path_s.substr(0, pos);
    strcpy(gamedir, (path_s + PATHSEPERATOR).c_str());
    logprint("gamedir: %s\n", gamedir);

    // See if it's the main game data folder (ID1 / baseq2 / data1 etc.)
    std::string gamename_s;
    pos = up_dir_pos(path_s, gamename_s);
    if (pos == -1) {
        logprint("SetQdirFromPath: invalid path: '%s'", path);
        ClearQdir();
        return;
    }

    // Not the main game data folder...
    if (!string_iequals(gamename_s, basedir_s)) {
        pos = 0;
        while (pos != -1) {
            const std::string checkpath_s = path_s + PATHSEPERATOR + basedir_s;
            if (dir_exists(checkpath_s.c_str())) {
                // Set basedir
                strcpy(basedir, (checkpath_s + PATHSEPERATOR).c_str());
                logprint("basedir: %s\n", basedir);
                break;
            }

            pos = up_dir_pos(path_s, gamename_s);
            path_s = path_s.substr(0, pos);
        }

        if (pos == -1) {
            logprint("SetQ2dirFromPath: failed to find %s in '%s'\n", basedir, path);
            ClearQdir();
            return;
        }

        // qdir is already in path_s
    } else {
        // Set basedir
        strcpy(basedir, (path_s + PATHSEPERATOR).c_str());
        logprint("basedir: %s\n", basedir);

        // qdir shound be 1 level above basedir
        pos = up_dir_pos(path_s, gamename_s);
        path_s = path_s.substr(0, pos);
    }

    // Store qdir...
    strcpy(qdir, (path_s + PATHSEPERATOR).c_str());
    logprint("qdir:    %s\n", qdir);
}

char *
ExpandPath(char *path)
{
    static char full[1024];

    if (!qdir[0])
        Error("ExpandPath called without qdir set");
    if (path[0] == '/' || path[0] == '\\' || path[1] == ':')
        return path;
    sprintf(full, "%s%s", qdir, path);
    return full;
}

char *
ExpandPathAndArchive(char *path)
{
    char *expanded;
    char archivename[1024];

    expanded = ExpandPath(path);

    if (archive) {
        sprintf(archivename, "%s/%s", archivedir, path);
        Q_CopyFile(expanded, archivename);
    }
    return expanded;
}

char *
copystring(const char *s)
{
    char *b;

    b = static_cast<char *>(malloc(strlen(s) + 1));
    strcpy(b, s);
    return b;
}


/*
 * ================
 * I_FloatTime
 * ================
 */
double
I_FloatTime(void)
{
#ifdef WIN32
        FILETIME ft;
        uint64_t hundred_ns;
        GetSystemTimeAsFileTime(&ft);
        hundred_ns = (((uint64_t)ft.dwHighDateTime) << 32) + ((uint64_t)ft.dwLowDateTime);
        return (double)hundred_ns / 10000000.0;
#else
    struct timeval tv;
    
    gettimeofday(&tv, NULL);
    
    return (double)tv.tv_sec + (tv.tv_usec / 1000000.0);
#endif
}

void
Q_getwd(char *out)
{
#ifdef WIN32
    _getcwd(out, 256);
    strcat(out, "\\");
#else
#ifdef LINUX

    char *pwd;
    int len;

    pwd = getenv("PWD");
    if (!pwd)
        Error("Couldn't get working directory - "
              "PWD not set in environment\n");
    len = strlen(pwd);
    if (len > 255)
        Error("Not enough space to hold working dir\n");
    strcpy(out, pwd);

#else
    getwd(out);
#endif
#endif
}

void
Q_mkdir(const char *path)
{
#ifdef WIN32
    if (_mkdir(path) != -1)
        return;
#else
    if (mkdir(path, 0777) != -1)
        return;
#endif
    if (errno != EEXIST)
        Error("mkdir %s: %s", path, strerror(errno));
}

/*
 * ============
 * FileTime
 * returns -1 if not present
 * ============
 */
int
FileTime(const char *path)
{
    struct stat buf;

    if (stat(path, &buf) == -1)
        return -1;

    return buf.st_mtime;
}

/*
 * ==============
 * COM_Parse
 * Parse a token out of a string
 * ==============
 */
const char *
COM_Parse(const char *data)
{
    int c;
    int len;

    len = 0;
    com_token[0] = 0;

    if (!data)
        return NULL;

    /* skip whitespace */
  skipwhite:
    while ((c = *data) <= ' ') {
        if (c == 0) {
            com_eof = true;
            return NULL;        /* end of file; */
        }
        data++;
    }

    /* skip // (double forward slash) comments */
    if (c == '/' && data[1] == '/') {
        while (*data && *data != '\n')
            data++;
        goto skipwhite;
    }

    /* skip C-style comments */
    if (c == '/' && data[1] == '*') {
        data += 2;
        while (*data && !(*data == '*' && data[1] == '/'))
            data++;
        if (*data)
            data += 2;
        goto skipwhite;
    }

    /* handle quoted strings specially */
    if (c == '\"') {
        data++;
        do {
            c = *data;
            if (c)
                data++;
            if (c == '\"') {
                com_token[len] = 0;
                return data;
            }
            com_token[len] = c;
            len++;
        } while (1);
    }

    /* parse single characters */
    if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':') {
        com_token[len] = c;
        len++;
        com_token[len] = 0;
        return data + 1;
    }

    /* parse a regular word */
    do {
        com_token[len] = c;
        data++;
        len++;
        c = *data;
        if (c == '{' || c == '}' || c == ')' || c == '(' || c == '\''
            || c == ':')
            break;
    } while (c > 32);

    com_token[len] = 0;
    return data;
}

int
Q_strncasecmp(const char *s1, const char *s2, int n)
{
    int c1, c2;

    while (1) {
        c1 = *s1++;
        c2 = *s2++;

        if (!n--)
            return 0;           /* strings are equal until end point */

        if (c1 != c2) {
            if (c1 >= 'a' && c1 <= 'z')
                c1 -= ('a' - 'A');
            if (c2 >= 'a' && c2 <= 'z')
                c2 -= ('a' - 'A');
            if (c1 != c2)
                return -1;      /* strings not equal */
        }
        if (!c1)
            return 0;           /* strings are equal */
    }

    return -1;
}

int
Q_strcasecmp(const char *s1, const char *s2)
{
    return Q_strncasecmp(s1, s2, 99999);
}

char *
Q_strupr(char *start)
{
    char *in;

    in = start;
    while (*in) {
        *in = toupper(*in);
        in++;
    }
    return start;
}

char *
Q_strlower(char *start)
{
    char *in;

    in = start;
    while (*in) {
        *in = tolower(*in);
        in++;
    }
    return start;
}


/* ============================================================================
 *                                 MISC FUNCTIONS
 * ============================================================================
 */

/*
 * =================
 * CheckParm
 * Checks for the given parameter in the program's command line arguments
 * Returns the argument number (1 to argc-1) or 0 if not present
 * =================
 */
int
CheckParm(const char *check)
{
    int i;

    for (i = 1; i < myargc; i++) {
        if (!Q_strcasecmp(check, myargv[i]))
            return i;
    }

    return 0;
}


/*
 * ===========
 * filelength
 * ===========
 */
int
Sys_filelength(FILE *f)
{
    int pos;
    int end;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

FILE *
SafeOpenWrite(const char *filename)
{
    FILE *f;

    f = fopen(filename, "wb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

FILE *
SafeOpenRead(const char *filename)
{
    FILE *f;

    f = fopen(filename, "rb");

    if (!f)
        Error("Error opening %s: %s", filename, strerror(errno));

    return f;
}

void
SafeRead(FILE *f, void *buffer, int count)
{
    if (fread(buffer, 1, count, f) != (size_t) count)
        Error("File read failure");
}

void
SafeWrite(FILE *f, const void *buffer, int count)
{
    const size_t written = fwrite(buffer, 1, count, f);
    if (written != (size_t) count)
        Error("File write failure");
}

typedef struct {
        char magic[4];
        unsigned int tableofs;
        unsigned int numfiles;
} pakheader_t;
typedef struct {
        char name[56];
        unsigned int offset;
        unsigned int length;
} pakfile_t;

/*
 * ==============
 * LoadFilePak
 * reads a file directly out of a pak, to make re-lighting friendlier
 * writes to the filename, stripping the pak part of the name
 * ==============
 */
int
LoadFilePak(char *filename, void *destptr)
{
    uint8_t **bufferptr = static_cast<uint8_t **>(destptr);
    uint8_t *buffer;
    FILE *file;
    int length;
    char *e = NULL;

    file = fopen(filename, "rb");
    if (!file)
    {
        e = filename + strlen(filename);
        for( ; e>filename ; )
        {
            while(e > filename)
                if (*--e == '/')
                        break;
            if (*e == '/')
            {
                *e = 0;
                file = fopen(filename, "rb");
                if (file)
                {
                    uint8_t **bufferptr = static_cast<uint8_t **>(destptr);
                    pakheader_t header;
                    unsigned int i;
                    const char *innerfile = e+1;
                    length = -1;
                    SafeRead(file, &header, sizeof(header));

                    header.numfiles = LittleLong(header.numfiles) / sizeof(pakfile_t);
                    header.tableofs = LittleLong(header.tableofs);

                    if (!strncmp(header.magic, "PACK", 4))
                    {
                        pakfile_t *files = static_cast<pakfile_t *>(malloc(header.numfiles * sizeof(*files)));
//                      printf("%s: %u files\n", pakfilename, header.numfiles);
                        fseek(file, header.tableofs, SEEK_SET);
                        SafeRead(file, files, header.numfiles * sizeof(*files));

                        for (i = 0; i < header.numfiles; i++)
                        {
                                if (!strcmp(files[i].name, innerfile))
                                {
                                        fseek(file, files[i].offset, SEEK_SET);
                                        *bufferptr = static_cast<uint8_t*>(malloc(files[i].length + 1));
                                        SafeRead(file, *bufferptr, files[i].length);
                                        length = files[i].length;
                                        break;
                                }
                        }
                        free(files);
                    }

                    fclose(file);
                    if (length < 0)
                        Error("Unable to find %s inside %s", innerfile, filename);

                    while(e > filename)
                        if (*--e == '/')
                        {
                            strcpy(e+1, innerfile);
                            return length;
                        }
                    strcpy(filename, innerfile);
                    return length;
                }
                *e = '/';
            }
        }
        Error("Error opening %s: %s", filename, strerror(errno));
    }


    file = SafeOpenRead(filename);
    length = Sys_filelength(file);
    buffer = *bufferptr = static_cast<uint8_t*>(malloc(length + 1));
    if (!buffer)
        Error("%s: allocation of %i bytes failed.", __func__, length);

    SafeRead(file, buffer, length);
    fclose(file);
    buffer[length] = 0;

    return length;
}

/*
 * ==============
 * LoadFile
 * ==============
 */
int
LoadFile(const char *filename, void *destptr)
{
    uint8_t **bufferptr = static_cast<uint8_t**>(destptr);
    uint8_t *buffer;
    FILE *file;
    int length;

    file = SafeOpenRead(filename);
    length = Sys_filelength(file);
    buffer = *bufferptr = static_cast<uint8_t*>(malloc(length + 1));
    if (!buffer)
        Error("%s: allocation of %i bytes failed.", __func__, length);

    SafeRead(file, buffer, length);
    fclose(file);
    buffer[length] = 0;

    return length;
}

/*
 * ==============
 * SaveFile
 * ==============
 */
void
SaveFile(const char *filename, const void *buffer, int count)
{
    FILE *f;

    f = SafeOpenWrite(filename);
    SafeWrite(f, buffer, count);
    fclose(f);
}

void
DefaultExtension(char *path, const char *extension)
{
    char *src;

    /* if path doesn't have a .EXT, append extension */
    /* (extension should include the .)              */
    src = path + strlen(path) - 1;

    while (*src != PATHSEPERATOR && src != path) {
        if (*src == '.')
            return;             /* it has an extension */
        src--;
    }

    strcat(path, extension);
}

void
DefaultPath(char *path, const char *basepath)
{
    char temp[128];

    if (path[0] == PATHSEPERATOR)
        return;                 /* absolute path location */
    strcpy(temp, path);
    strcpy(path, basepath);
    strcat(path, temp);
}

std::string
StrippedFilename(const std::string& path)
{
    const size_t lastSlash = path.rfind(PATHSEPERATOR);
    if (lastSlash == std::string::npos) {
        return std::string();
    }
    // excludes the trailing slash
    return path.substr(0, lastSlash);
}

void
StripExtension(char *path)
{
    int length;

    length = strlen(path) - 1;
    while (length > 0 && path[length] != '.') {
        length--;
        if (path[length] == '/')
            return;             /* no extension */
    }
    if (length)
        path[length] = 0;
}

std::string
StrippedExtension(const std::string& path) {
    std::string result = path;

    int length;
    length = static_cast<int>(path.size()) - 1;
    while (length > 0 && path[length] != '.') {
        length--;
        if (path[length] == '/')
            return path;             /* no extension */
    }
    if (length)
        result = result.substr(0, static_cast<size_t>(length - 1));

    return result;
}

int
IsAbsolutePath(const char *path)
{
    return path[0] == PATHSEPERATOR || (isalpha(path[0]) && path[1] == ':');
}

/*
 * ====================
 * Extract file parts
 * ====================
 */
void
ExtractFilePath(char *path, char *dest)
{
    char *src;

    src = path + strlen(path) - 1;

    /* back up until a \ or the start */
    while (src != path && *(src - 1) != PATHSEPERATOR)
        src--;

    memcpy(dest, path, src - path);
    dest[src - path] = 0;
}

void
ExtractFileBase(char *path, char *dest)
{
    char *src;

    src = path + strlen(path) - 1;

    /* back up until a \ or the start */
    while (src != path && *(src - 1) != PATHSEPERATOR)
        src--;

    while (*src && *src != '.') {
        *dest++ = *src++;
    }
    *dest = 0;
}

void
ExtractFileExtension(char *path, char *dest)
{
    char *src;

    src = path + strlen(path) - 1;

    /* back up until a . or the start */
    while (src != path && *(src - 1) != '.')
        src--;
    if (src == path) {
        *dest = 0;              /* no extension */
        return;
    }

    strcpy(dest, src);
}

/*
 * ==============
 * ParseNum / ParseHex
 * ==============
 */
int
ParseHex(char *hex)
{
    char *str;
    int num;

    num = 0;
    str = hex;

    while (*str) {
        num <<= 4;
        if (*str >= '0' && *str <= '9')
            num += *str - '0';
        else if (*str >= 'a' && *str <= 'f')
            num += 10 + *str - 'a';
        else if (*str >= 'A' && *str <= 'F')
            num += 10 + *str - 'A';
        else
            Error("Bad hex number: %s", hex);
        str++;
    }

    return num;
}

int
ParseNum(char *str)
{
    if (str[0] == '$')
        return ParseHex(str + 1);
    if (str[0] == '0' && str[1] == 'x')
        return ParseHex(str + 2);
    return atol(str);
}


/*
 * ============================================================================
 *                            BYTE ORDER FUNCTIONS
 * ============================================================================
 */

#ifdef _SGI_SOURCE
#define __BIG_ENDIAN__
#endif

#ifdef __BIG_ENDIAN__

short
LittleShort(short l)
{
    uint8_t b1, b2;

    b1 = l & 255;
    b2 = (l >> 8) & 255;

    return (b1 << 8) + b2;
}

short
BigShort(short l)
{
    return l;
}


int
LittleLong(int l)
{
    uint8_t b1, b2, b3, b4;

    b1 = l & 255;
    b2 = (l >> 8) & 255;
    b3 = (l >> 16) & 255;
    b4 = (l >> 24) & 255;

    return ((int)b1 << 24) + ((int)b2 << 16) + ((int)b3 << 8) + b4;
}

int
BigLong(int l)
{
    return l;
}

float
LittleFloat(float l)
{
    union {
        uint8_t b[4];
        float f;
    } in , out;

    in.f = l;
    out.b[0] = in.b[3];
    out.b[1] = in.b[2];
    out.b[2] = in.b[1];
    out.b[3] = in.b[0];

    return out.f;
}

float
BigFloat(float l)
{
    return l;
}


#else /* must be little endian */


short
BigShort(short l)
{
    uint8_t b1, b2;

    b1 = l & 255;
    b2 = (l >> 8) & 255;

    return (b1 << 8) + b2;
}

short
LittleShort(short l)
{
    return l;
}

int
BigLong(int l)
{
    uint8_t b1, b2, b3, b4;

    b1 = l & 255;
    b2 = (l >> 8) & 255;
    b3 = (l >> 16) & 255;
    b4 = (l >> 24) & 255;

    return ((int)b1 << 24) + ((int)b2 << 16) + ((int)b3 << 8) + b4;
}

int
LittleLong(int l)
{
    return l;
}

float
BigFloat(float l)
{
    union {
        uint8_t b[4];
        float f;
    } in , out;

    in.f = l;
    out.b[0] = in.b[3];
    out.b[1] = in.b[2];
    out.b[2] = in.b[1];
    out.b[3] = in.b[0];

    return out.f;
}

float
LittleFloat(float l)
{
    return l;
}


#endif

/* ========================================================================= */


/*
 * FIXME: byte swap?
 *
 * this is a 16 bit, non-reflected CRC using the polynomial 0x1021
 * and the initial and final xor values shown below...  in other words, the
 * CCITT standard CRC used by XMODEM
 */

#define CRC_INIT_VALUE    0xffff
#define CRC_XOR_VALUE    0x0000

static unsigned short crctable[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

void
CRC_Init(unsigned short *crcvalue)
{
    *crcvalue = CRC_INIT_VALUE;
}

void
CRC_ProcessByte(unsigned short *crcvalue, uint8_t data)
{
    *crcvalue = (*crcvalue << 8) ^ crctable[(*crcvalue >> 8) ^ data];
}

unsigned short
CRC_Value(unsigned short crcvalue)
{
    return crcvalue ^ CRC_XOR_VALUE;
}

unsigned short CRC_Block (const unsigned char *start, int count)
{
    unsigned short crc;
    CRC_Init (&crc);
    while (count--)
        crc = (crc << 8) ^ crctable[(crc >> 8) ^ *start++];
    return crc;
}

/* ========================================================================= */

/*
 * ============
 * CreatePath
 * ============
 */
void
CreatePath(char *path)
{
    char *ofs, c;

    for (ofs = path + 1; *ofs; ofs++) {
        c = *ofs;
        if (c == '/' || c == '\\') {    /* create the directory */
            *ofs = 0;
            Q_mkdir(path);
            *ofs = c;
        }
    }
}


/*
 * ============
 * Q_CopyFile
 * Used to archive source files
 *============
 */
void
Q_CopyFile(const char *from, char *to)
{
    void *buffer;
    int length;

    length = LoadFile(from, &buffer);
    CreatePath(to);
    SaveFile(to, buffer, length);
    free(buffer);
}

// from QuakeSpasm

/* platform dependant (v)snprintf function names: */
#if defined(_WIN32)
#define	snprintf_func		_snprintf
#define	vsnprintf_func		_vsnprintf
#else
#define	snprintf_func		snprintf
#define	vsnprintf_func		vsnprintf
#endif

int q_vsnprintf(char *str, size_t size, const char *format, va_list args)
{
	int		ret;

	ret = vsnprintf_func(str, size, format, args);

	if (ret < 0)
		ret = (int)size;
	if (size == 0)	/* no buffer */
		return ret;
	if ((size_t)ret >= size)
		str[size - 1] = '\0';

	return ret;
}

int q_snprintf(char *str, size_t size, const char *format, ...)
{
	int		ret;
	va_list		argptr;

	va_start(argptr, format);
	ret = q_vsnprintf(str, size, format, argptr);
	va_end(argptr);

	return ret;
}
