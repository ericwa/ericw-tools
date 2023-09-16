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

#include <sys/types.h>

#ifdef _WIN32
#include <windows.h>

// don't break std::min
#ifdef min
#undef min
#endif
#endif

#ifdef LINUX
#include <sys/time.h>
#include <unistd.h>
#include <cstring>
#endif

#include <cstdint>

#include <algorithm>
#include <string>

#if defined(__has_include) && __has_include(<strings.h>)
#include <strings.h>
#endif

int32_t Q_strncasecmp(const std::string_view &a, const std::string_view &b, size_t maxcount)
{
    return
#ifdef _WIN32
        _strnicmp
#elif defined(__has_include) && __has_include(<strings.h>)
        strncasecmp
#else
        strnicmp
#endif
        (a.data(), b.data(), maxcount);
}

int32_t Q_strcasecmp(const std::string_view &a, const std::string_view &b)
{
    return
#ifdef _WIN32
        _stricmp
#elif defined(__has_include) && __has_include(<strings.h>)
        strcasecmp
#else
        stricmp
#endif
        (a.data(), b.data());
}

void // mxd
string_replaceall(std::string &str, const std::string &from, const std::string &to)
{
    if (from.empty()) {
        return;
    }

    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
    }
}

bool // mxd
string_iequals(const std::string_view &a, const std::string_view &b)
{
    size_t sz = a.size();
    if (b.size() != sz)
        return false;
    for (size_t i = 0; i < sz; ++i)
        if (tolower(a[i]) != tolower(b[i]))
            return false;
    return true;
}

std::size_t case_insensitive_hash::operator()(const std::string &s) const noexcept
{
    std::size_t hash = 0x811c9dc5;
    constexpr std::size_t prime = 0x1000193;

    for (auto &c : s) {
        hash ^= tolower(c);
        hash *= prime;
    }

    return hash;
}

bool case_insensitive_equal::operator()(const std::string &l, const std::string &r) const noexcept
{
    return Q_strcasecmp(l.c_str(), r.c_str()) == 0;
}

bool case_insensitive_less::operator()(const std::string &l, const std::string &r) const noexcept
{
    return Q_strcasecmp(l.c_str(), r.c_str()) < 0;
}

// membuf

membuf::membuf(void *base, size_t size, std::ios_base::openmode which)
{
    auto cbase = reinterpret_cast<char *>(base);

    if (which & std::ios_base::in) {
        this->setg(cbase, cbase, cbase + size);
    }

    if (which & std::ios_base::out) {
        this->setp(cbase, cbase + size);
    }
}

membuf::membuf(const void *base, size_t size, std::ios_base::openmode which)
{
    auto cbase = const_cast<char *>(reinterpret_cast<const char *>(base));

    if (which & std::ios_base::in) {
        this->setg(cbase, cbase, cbase + size);
    }
}

void membuf::setpptrs(char *first, char *next, char *end)
{
    setp(first, end);
    pbump(next - first);
}

membuf::pos_type membuf::seekpos(pos_type off, std::ios_base::openmode which)
{
    if (which & std::ios_base::in) {
        setg(eback(), eback() + off, egptr());
    }

    if (which & std::ios_base::out) {
        setpptrs(pbase(), pbase() + off, epptr());
    }

    if (which & std::ios_base::in) {
        return gptr() - eback();
    } else {
        return pptr() - pbase();
    }
}

membuf::pos_type membuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
{
    if (which & std::ios_base::in) {
        if (dir == std::ios_base::cur)
            gbump(off);
        else if (dir == std::ios_base::end)
            setg(eback(), egptr() + off, egptr());
        else if (dir == std::ios_base::beg)
            setg(eback(), eback() + off, egptr());
    }

    if (which & std::ios_base::out) {
        if (dir == std::ios_base::cur)
            pbump(off);
        else if (dir == std::ios_base::end)
            setpptrs(pbase(), epptr() + off, epptr());
        else if (dir == std::ios_base::beg)
            setpptrs(pbase(), pbase() + off, epptr());
    }

    if (which & std::ios_base::in) {
        return gptr() - eback();
    } else {
        return pptr() - pbase();
    }
}

std::streamsize membuf::xsputn(const char_type *s, std::streamsize n)
{
    if (pptr() == epptr()) {
        return traits_type::eof();
    }

    std::streamsize free_space = epptr() - pptr();
    std::streamsize num_write = std::min(free_space, n);

    memcpy(pptr(), s, n);
    setpptrs(pbase(), pptr() + n, epptr());

    return num_write;
}

membuf::int_type membuf::overflow(int_type ch)
{
    return traits_type::eof();
}

std::streamsize membuf::xsgetn(char_type *s, std::streamsize n)
{
    if (gptr() == egptr()) {
        return traits_type::eof();
    }

    std::streamsize free_space = egptr() - gptr();
    std::streamsize num_read = std::min(free_space, n);

    memcpy(s, gptr(), n);
    setg(eback(), gptr() + n, egptr());

    return num_read;
}

membuf::int_type membuf::underflow()
{
    return traits_type::eof();
}

// memstream
memstream::memstream(void *base, size_t size, std::ios_base::openmode which)
    : membuf(base, size, which),
      std::ostream(static_cast<std::streambuf *>(this)),
      std::istream(static_cast<std::streambuf *>(this))
{
}

memstream::memstream(const void *base, size_t size, std::ios_base::openmode which)
    : membuf(base, size, which),
      std::ostream(nullptr),
      std::istream(static_cast<std::streambuf *>(this))
{
}

// omemstream

omemstream::omemstream(void *base, size_t size, std::ios_base::openmode which)
    : membuf(base, size, which),
      std::ostream(static_cast<std::streambuf *>(this))
{
}

// imemstream

imemstream::imemstream(const void *base, size_t size, std::ios_base::openmode which)
    : membuf(base, size, which),
      std::istream(static_cast<std::streambuf *>(this))
{
}

// omemsizebuf

omemsizebuf::omemsizebuf(std::ios_base::openmode which)
{
    if (which & std::ios_base::in) {
        throw std::invalid_argument("which");
    }

    this->setp(nullptr, nullptr);
}

void omemsizebuf::setpptrs(char *first, char *next, char *end)
{
    setp(first, end);
    pbump(next - first);
}

omemsizebuf::pos_type omemsizebuf::seekpos(pos_type off, std::ios_base::openmode which)
{
    setpptrs(pbase(), pbase() + off, epptr());

    return pptr() - pbase();
}

omemsizebuf::pos_type omemsizebuf::seekoff(off_type off, std::ios_base::seekdir dir, std::ios_base::openmode which)
{
    if (dir == std::ios_base::cur)
        pbump(off);
    else if (dir == std::ios_base::end)
        setpptrs(pbase(), epptr() + off, epptr());
    else if (dir == std::ios_base::beg)
        setpptrs(pbase(), pbase() + off, epptr());

    return pptr() - pbase();
}

std::streamsize omemsizebuf::xsputn(const char_type *s, std::streamsize n)
{
    if (pptr() == epptr()) {
        setpptrs(pbase(), epptr(), epptr() + n);
    }

    std::streamsize free_space = epptr() - pptr();
    std::streamsize num_write = std::min(free_space, n);

    setpptrs(pbase(), pptr() + n, epptr());

    return num_write;
}

omemsizebuf::int_type omemsizebuf::overflow(int_type ch)
{
    setpptrs(pbase(), epptr(), epptr() + 1);
    return ch;
}

// omemsizestream

omemsizestream::omemsizestream(std::ios_base::openmode which)
    : omemsizebuf(which),
      std::ostream(static_cast<std::streambuf *>(this))
{
}

/* ========================================================================= */

/*
 * FIXME: byte swap?
 *
 * this is a 16 bit, non-reflected CRC using the polynomial 0x1021
 * and the initial and final xor values shown below...  in other words, the
 * CCITT standard CRC used by XMODEM
 */

constexpr uint16_t CRC_INIT_VALUE = 0xffff;

static uint16_t crctable[256] = {0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7, 0x8108, 0x9129, 0xa14a,
    0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef, 0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6, 0x9339,
    0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de, 0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4,
    0x5485, 0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d, 0x3653, 0x2672, 0x1611, 0x0630, 0x76d7,
    0x66f6, 0x5695, 0x46b4, 0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc, 0x48c4, 0x58e5, 0x6886,
    0x78a7, 0x0840, 0x1861, 0x2802, 0x3823, 0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b, 0x5af5,
    0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12, 0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b,
    0xab1a, 0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41, 0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a,
    0xbd0b, 0x8d68, 0x9d49, 0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70, 0xff9f, 0xefbe, 0xdfdd,
    0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78, 0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f, 0x1080,
    0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067, 0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f,
    0xf35e, 0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256, 0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e,
    0xe54f, 0xd52c, 0xc50d, 0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405, 0xa7db, 0xb7fa, 0x8799,
    0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c, 0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634, 0xd94c,
    0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab, 0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882,
    0x28a3, 0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a, 0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1,
    0x1ad0, 0x2ab3, 0x3a92, 0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 0x7c26, 0x6c07, 0x5c64,
    0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 0x6e17,
    0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0};

void CRC_Init(uint16_t &crcvalue)
{
    crcvalue = CRC_INIT_VALUE;
}

void CRC_ProcessByte(uint16_t &crcvalue, uint8_t data)
{
    crcvalue = (crcvalue << 8) ^ crctable[(crcvalue >> 8) ^ data];
}

uint16_t CRC_Block(const uint8_t *start, int count)
{
    uint16_t crc;
    CRC_Init(crc);
    while (count--)
        CRC_ProcessByte(crc, *start++);
    return crc;
}

/**
//========================================================================
// Copyright (c) 1998-2010,2011 Free Software Foundation, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, distribute with modifications, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
// THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// Except as contained in this notice, the name(s) of the above copyright
// holders shall not be used in advertising or otherwise to promote the
// sale, use or other dealings in this Software without prior written
// authorization.
//========================================================================

//========================================================================
//  Author: Jan-Marten Spit <jmspit@euronet.nl>
//========================================================================
 */

inline bool t_digit(char c)
{
    return c >= 48 && c <= 57;
}

inline char t_lower(char c)
{
    return std::tolower(c);
}

int natstrcmp(const char *s1, const char *s2, bool case_sensitive)
{
    const char *p1 = s1;
    const char *p2 = s2;
    const unsigned short st_scan = 0;
    const unsigned short st_alpha = 1;
    const unsigned short st_numeric = 2;
    unsigned short state = st_scan;
    const char *numstart1 = 0;
    const char *numstart2 = 0;
    const char *numend1 = 0;
    const char *numend2 = 0;
    unsigned long sz1 = 0;
    unsigned long sz2 = 0;
    while (*p1 && *p2) {
        switch (state) {
            case st_scan:
                if (!t_digit(*p1) && !t_digit(*p2)) {
                    state = st_alpha;
                    if (case_sensitive ? (*p1 == *p2) : (t_lower(*p1) == t_lower(*p2))) {
                        p1++;
                        p2++;
                    } else if (case_sensitive ? (*p1 < *p2) : (t_lower(*p1) < t_lower(*p2)))
                        return -1;
                    else
                        return 1;
                } else if (t_digit(*p1) && !t_digit(*p2))
                    return -1;
                else if (!t_digit(*p1) && t_digit(*p2))
                    return 1;
                else {
                    if (sz1 == 0)
                        while (*p1 == '0') {
                            p1++;
                            sz1++;
                        }
                    else
                        while (*p1 == '0')
                            p1++;
                    if (sz2 == 0)
                        while (*p2 == '0') {
                            p2++;
                            sz2++;
                        }
                    else
                        while (*p2 == '0')
                            p2++;
                    if (sz1 == sz2) {
                        sz1 = 0;
                        sz2 = 0;
                    };
                    if (!t_digit(*p1))
                        p1--;
                    if (!t_digit(*p2))
                        p2--;
                    numstart1 = p1;
                    numstart2 = p2;
                    numend1 = numstart1;
                    numend2 = numstart2;
                    p1++;
                    p2++;
                }
                break;
            case st_alpha:
                if (!t_digit(*p1) && !t_digit(*p2)) {
                    if (case_sensitive ? (*p1 == *p2) : (t_lower(*p1) == t_lower(*p2))) {
                        p1++;
                        p2++;
                    } else if (case_sensitive ? (*p1 < *p2) : (t_lower(*p1) < t_lower(*p2)))
                        return -1;
                    else
                        return 1;
                } else
                    state = st_scan;
                break;
            case st_numeric:
                while (t_digit(*p1))
                    numend1 = p1++;
                while (t_digit(*p2))
                    numend2 = p2++;
                if (numend1 - numstart1 == numend2 - numstart2 && case_sensitive
                        ? (!strncmp(numstart1, numstart2, numend2 - numstart2 + 1))
                        : (!Q_strncasecmp(numstart1, numstart2, numend2 - numstart2 + 1)))
                    state = st_scan;
                else {
                    if (numend1 - numstart1 < numend2 - numstart2)
                        return -1;
                    if (numend1 - numstart1 > numend2 - numstart2)
                        return 1;
                    while (*numstart1 && *numstart2) {
                        if (*numstart1 < *numstart2)
                            return -1;
                        if (*numstart1 > *numstart2)
                            return 1;
                        numstart1++;
                        numstart2++;
                    }
                }
                break;
        }
    }
    if (sz1 < sz2)
        return -1;
    if (sz1 > sz2)
        return 1;
    if (*p1 == 0 && *p2 != 0)
        return -1;
    if (*p1 != 0 && *p2 == 0)
        return 1;
    return 0;
}

/**
 * STL natural less-than string compare
 * @return true when natural s1 < s2
 */
bool natstrlt(const char *s1, const char *s2, bool case_sensitive)
{
    // std::cout << "natstrlt s1=" << s1 << " s2=" << s2 << std::endl;
    const char *p1 = s1;
    const char *p2 = s2;
    const unsigned short st_scan = 0;
    const unsigned short st_alpha = 1;
    const unsigned short st_numeric = 2;
    unsigned short state = st_scan;
    const char *numstart1 = 0;
    const char *numstart2 = 0;
    const char *numend1 = 0;
    const char *numend2 = 0;
    unsigned long sz1 = 0;
    unsigned long sz2 = 0;
    while (*p1 && *p2) {
        switch (state) {
            case st_scan:
                if (!t_digit(*p1) && !t_digit(*p2)) {
                    state = st_alpha;
                    if (case_sensitive ? (*p1 == *p2) : (t_lower(*p1) == t_lower(*p2))) {
                        p1++;
                        p2++;
                    } else
                        return case_sensitive ? (*p1 < *p2) : (t_lower(*p1) < t_lower(*p2));
                } else if (t_digit(*p1) && !t_digit(*p2))
                    return true;
                else if (!t_digit(*p1) && t_digit(*p2))
                    return false;
                else {
                    state = st_numeric;
                    if (sz1 == 0)
                        while (*p1 == '0') {
                            p1++;
                            sz1++;
                        }
                    else
                        while (*p1 == '0')
                            p1++;
                    if (sz2 == 0)
                        while (*p2 == '0') {
                            p2++;
                            sz2++;
                        }
                    else
                        while (*p2 == '0')
                            p2++;
                    if (sz1 == sz2) {
                        sz1 = 0;
                        sz2 = 0;
                    };
                    if (!t_digit(*p1))
                        p1--;
                    if (!t_digit(*p2))
                        p2--;
                    numstart1 = p1;
                    numstart2 = p2;
                    numend1 = numstart1;
                    numend2 = numstart2;
                }
                break;
            case st_alpha:
                if (!t_digit(*p1) && !t_digit(*p2)) {
                    if (case_sensitive ? (*p1 == *p2) : (t_lower(*p1) == t_lower(*p2))) {
                        p1++;
                        p2++;
                    } else
                        return case_sensitive ? (*p1 < *p2) : (t_lower(*p1) < t_lower(*p2));
                } else
                    state = st_scan;
                break;
            case st_numeric:
                while (t_digit(*p1))
                    numend1 = p1++;
                while (t_digit(*p2))
                    numend2 = p2++;
                if (numend1 - numstart1 == numend2 - numstart2 &&
                    !strncmp(numstart1, numstart2, numend2 - numstart2 + 1))
                    state = st_scan;
                else {
                    if (numend1 - numstart1 != numend2 - numstart2)
                        return numend1 - numstart1 < numend2 - numstart2;
                    while (*numstart1 && *numstart2) {
                        if (*numstart1 != *numstart2)
                            return *numstart1 < *numstart2;
                        numstart1++;
                        numstart2++;
                    }
                }
                break;
        }
    }
    if (sz1 < sz2)
        return true;
    if (sz1 > sz2)
        return false;
    if (*p1 == 0 && *p2 != 0)
        return true;
    if (*p1 != 0 && *p2 == 0)
        return false;
    return false;
}

bool stlnatstrlt(const std::string &s1, const std::string &s2, bool case_sensitive)
{
    return natstrlt(s1.c_str(), s2.c_str(), case_sensitive);
}

bool natural_equal::operator()(const std::string &l, const std::string &r) const noexcept
{
    return strcmp(l.c_str(), r.c_str()) == 0;
}

bool natural_less::operator()(const std::string &l, const std::string &r) const noexcept
{
    return stlnatstrlt(l, r);
}

bool natural_case_insensitive_equal::operator()(const std::string &l, const std::string &r) const noexcept
{
    return Q_strcasecmp(l.c_str(), r.c_str()) == 0;
}

bool natural_case_insensitive_less::operator()(const std::string &l, const std::string &r) const noexcept
{
    return stlnatstrlt(l, r, false);
}

std::string_view::const_iterator string_ifind(std::string_view haystack, std::string_view needle)
{
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return tolower(a) == tolower(b); });
}

bool string_icontains(std::string_view haystack, std::string_view needle)
{
    return string_ifind(haystack, needle) != haystack.end();
}

time_point I_FloatTime()
{
    return qclock::now();
}

namespace detail
{
int32_t endian_i()
{
    static int32_t i = std::ios_base::xalloc();
    return i;
}

bool need_swap(std::ios_base &os)
{
    st_en e = static_cast<st_en>(os.iword(detail::endian_i()));

    // if we're in a "default state" of native endianness, we never
    // need to swap.
    if (e == st_en::na)
        return false;

    return (static_cast<int32_t>(e) - 1) != static_cast<int32_t>(std::endian::native);
}
} // namespace detail

void *q_aligned_malloc(size_t align, size_t size)
{
#ifdef _WIN32
    return _aligned_malloc(size, align);
#elif __STDC_VERSION__ >= 201112L
    return aligned_alloc(align, size);
#else
    void *ptr;
    if (0 != posix_memalign(&ptr, align, size)) {
        return nullptr;
    }
    return ptr;
#endif
}

void q_aligned_free(void *ptr)
{
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
