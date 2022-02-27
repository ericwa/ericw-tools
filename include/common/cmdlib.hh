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

#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <cctype>
#include <ctime>
#include <cstdarg>
#include <string>
#include <memory>
#include <fmt/format.h>
#include <common/log.hh>

#define stringify__(x) #x
#define stringify(x) stringify__(x)

#ifdef _WIN32
#define Q_strncasecmp _strnicmp
#define Q_strcasecmp _stricmp
#elif defined(__has_include) && __has_include(<strings.h>)
#include <strings.h>
#define Q_strncasecmp strncasecmp
#define Q_strcasecmp strcasecmp
#else
#define Q_strncasecmp strnicmp
#define Q_strcasecmp stricmp
#endif

bool string_iequals(const std::string &a, const std::string &b); // mxd

struct case_insensitive_hash
{
    std::size_t operator()(const std::string &s) const noexcept
    {
        std::size_t hash = 0x811c9dc5;
        constexpr std::size_t prime = 0x1000193;

        for (auto &c : s) {
            hash ^= tolower(c);
            hash *= prime;
        }

        return hash;
    }
};

struct case_insensitive_equal
{
    bool operator()(const std::string &l, const std::string &r) const noexcept
    {
        return Q_strcasecmp(l.c_str(), r.c_str()) == 0;
    }
};

struct case_insensitive_less
{
    bool operator()(const std::string &l, const std::string &r) const noexcept
    {
        return Q_strcasecmp(l.c_str(), r.c_str()) < 0;
    }
};

// natural sorting
#include <string>

/**
 * standard C natural string compare
 * @param s1 left string
 * @param s2 right string
 * @return -1 when s1 < s2, 0 when s1 == s2, 1 when s1 > s2
 */
int natstrcmp(const char *s1, const char *s2);

/**
 * STL natural less-than string compare
 * @param s1 left string
 * @param s2 right string
 * @return true when natural s1 < s2
 */
bool natstrlt(const char *s1, const char *s2);

/**
 * @param s1 left string
 * @param s2 right string
 * std::string variant of natstrlt.
 * @return true when natural s1 < s2
 */
inline bool stlnatstrlt(const std::string &s1, const std::string &s2)
{
    return natstrlt(s1.c_str(), s2.c_str());
}

struct natural_equal
{
    bool operator()(const std::string &l, const std::string &r) const noexcept
    {
        return strcmp(l.c_str(), r.c_str()) == 0;
    }
};

struct natural_less
{
    bool operator()(const std::string &l, const std::string &r) const noexcept { return stlnatstrlt(l, r); }
};

#include <chrono>

using qclock = std::chrono::high_resolution_clock;
using duration = std::chrono::duration<double>;
using time_point = std::chrono::time_point<qclock, duration>;

inline time_point I_FloatTime()
{
    return qclock::now();
}

[[noreturn]] void Error(const char *error);

template<typename... Args>
[[noreturn]] inline void Error(const char *fmt, const Args &...args)
{
    auto formatted = fmt::format(fmt, std::forward<const Args &>(args)...);
    Error(formatted.c_str());
}

#define FError(fmt, ...) Error("{}: " fmt, __func__, ##__VA_ARGS__)

/*
 * ============================================================================
 *                            BYTE ORDER FUNCTIONS
 * ============================================================================
 */
// C++20 polyfill
// For cpp20, #include <bit> instead
namespace std
{
enum class endian
{
    little = 0,
    big = 1,

#ifdef __BIG_ENDIAN__
    native = big
#else
    native = little
#endif
};
} // namespace std

// C/C++ portable and defined method of swapping bytes.
template<typename T>
inline T byte_swap(const T &val)
{
    T retVal;
    const char *pVal = reinterpret_cast<const char *>(&val);
    char *pRetVal = reinterpret_cast<char *>(&retVal);

    for (size_t i = 0; i < sizeof(T); i++) {
        pRetVal[sizeof(T) - 1 - i] = pVal[i];
    }
    unsigned short CRC_Block(const unsigned char *start, int count);

    return retVal;
}

// little <-> native
inline int16_t LittleShort(int16_t l)
{
    if constexpr (std::endian::native == std::endian::little)
        return l;
    else
        return byte_swap(l);
}

inline int32_t LittleLong(int32_t l)
{
    if constexpr (std::endian::native == std::endian::little)
        return l;
    else
        return byte_swap(l);
}

inline float LittleFloat(float l)
{
    if constexpr (std::endian::native == std::endian::little)
        return l;
    else
        return byte_swap(l);
}

// big <-> native
inline int16_t BigShort(int16_t l)
{
    if constexpr (std::endian::native == std::endian::big)
        return l;
    else
        return byte_swap(l);
}

inline int32_t BigLong(int32_t l)
{
    if constexpr (std::endian::native == std::endian::big)
        return l;
    else
        return byte_swap(l);
}

inline float BigFloat(float l)
{
    if constexpr (std::endian::native == std::endian::big)
        return l;
    else
        return byte_swap(l);
}

inline void Q_assert_(bool success, const char *expr, const char *file, int line)
{
    if (!success) {
        LogPrint("{}:{}: Q_assert({}) failed.\n", file, line, expr);
        // assert(0);
#ifdef _WIN32
        __debugbreak();
#endif
        exit(1);
    }
}

/**
 * assertion macro that is used in all builds (debug/release)
 */
#define Q_assert(x) Q_assert_((x), stringify(x), __FILE__, __LINE__)

#define Q_assert_unreachable() Q_assert(false)

// Binary streams; by default, streams use the native endianness
// (unchanged bytes) but can be changed to a specific endianness
// with the manipulator below.
namespace detail
{
inline int32_t endian_i()
{
    static int32_t i = std::ios_base::xalloc();
    return i;
}

// 0 is the default for iwords
enum class st_en : long
{
    na = 0,
    le = 1,
    be = 2,
};

inline bool need_swap(std::ios_base &os)
{
    st_en e = static_cast<st_en>(os.iword(detail::endian_i()));

    // if we're in a "default state" of native endianness, we never
    // need to swap.
    if (e == st_en::na)
        return false;

    return (static_cast<int32_t>(e) - 1) != static_cast<int32_t>(std::endian::native);
}

template<typename T>
inline void write_swapped(std::ostream &s, const T &val)
{
    const char *pVal = reinterpret_cast<const char *>(&val);

    for (int32_t i = sizeof(T) - 1; i >= 0; i--) {
        s.write(&pVal[i], 1);
    }
}

template<typename T>
inline void read_swapped(std::istream &s, T &val)
{
    char *pRetVal = reinterpret_cast<char *>(&val);

    for (int32_t i = sizeof(T) - 1; i >= 0; i--) {
        s.read(&pRetVal[i], 1);
    }
}
} // namespace detail

template<std::endian e>
inline std::ios_base &endianness(std::ios_base &os)
{
    os.iword(detail::endian_i()) = static_cast<int32_t>(e) + 1;

    return os;
}

// blank type used
template<size_t n>
struct padding
{
};

// using <= for ostream and >= for istream
template<size_t n>
inline std::ostream &operator<=(std::ostream &s, const padding<n> &)
{
    for (size_t i = 0; i < n; i++) {
        s.put(0);
    }

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const char &c)
{
    s.write(&c, sizeof(c));

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const int8_t &c)
{
    s.write(reinterpret_cast<const char *>(&c), sizeof(c));

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const uint8_t &c)
{
    s.write(reinterpret_cast<const char *>(&c), sizeof(c));

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const uint16_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const int16_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const uint32_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const int32_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const uint64_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const int64_t &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const float &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

inline std::ostream &operator<=(std::ostream &s, const double &c)
{
    if (!detail::need_swap(s))
        s.write(reinterpret_cast<const char *>(&c), sizeof(c));
    else
        detail::write_swapped(s, c);

    return s;
}

template<typename T, size_t N>
inline std::ostream &operator<=(std::ostream &s, const std::array<T, N> &c)
{
    for (auto &v : c)
        s <= v;

    return s;
}

template<typename... T>
inline std::ostream &operator<=(std::ostream &s, std::tuple<T &...> tuple)
{
    std::apply([&s](auto &&...args) { ((s <= args), ...); }, tuple);
    return s;
}

template<typename T>
inline std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_data)>, std::ostream &> operator<=(
    std::ostream &s, const T &obj)
{
    // A big ugly, but, this skips us needing a const version of stream_data()
    s <= const_cast<T &>(obj).stream_data();
    return s;
}

template<typename T>
inline std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_write)>, std::ostream &> operator<=(
    std::ostream &s, const T &obj)
{
    obj.stream_write(s);
    return s;
}

template<size_t n>
inline std::istream &operator>=(std::istream &s, padding<n> &)
{
    s.seekg(n, std::ios_base::cur);

    return s;
}

inline std::istream &operator>=(std::istream &s, char &c)
{
    s.read(&c, sizeof(c));

    return s;
}

inline std::istream &operator>=(std::istream &s, int8_t &c)
{
    s.read(reinterpret_cast<char *>(&c), sizeof(c));

    return s;
}

inline std::istream &operator>=(std::istream &s, uint8_t &c)
{
    s.read(reinterpret_cast<char *>(&c), sizeof(c));

    return s;
}

inline std::istream &operator>=(std::istream &s, uint16_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, int16_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, uint32_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, int32_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, uint64_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, int64_t &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, float &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

inline std::istream &operator>=(std::istream &s, double &c)
{
    if (!detail::need_swap(s))
        s.read(reinterpret_cast<char *>(&c), sizeof(c));
    else
        detail::read_swapped(s, c);

    return s;
}

template<typename T, size_t N>
inline std::istream &operator>=(std::istream &s, std::array<T, N> &c)
{
    for (auto &v : c)
        s >= v;

    return s;
}

template<typename... T>
inline std::istream &operator>=(std::istream &s, std::tuple<T &...> tuple)
{
    std::apply([&s](auto &&...args) { ((s >= args), ...); }, tuple);
    return s;
}

template<typename T>
inline std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_data)>, std::istream &> operator>=(
    std::istream &s, T &obj)
{
    s >= obj.stream_data();
    return s;
}

template<typename T>
inline std::enable_if_t<std::is_member_function_pointer_v<decltype(&T::stream_read)>, std::istream &> operator>=(
    std::istream &s, T &obj)
{
    obj.stream_read(s);
    return s;
}

template<typename Dst, typename Src>
constexpr bool numeric_cast_will_overflow(const Src &value)
{
    using DstLim = std::numeric_limits<Dst>;
    using SrcLim = std::numeric_limits<Src>;

    constexpr bool positive_overflow_possible = DstLim::max() < SrcLim::max();
    constexpr bool negative_overflow_possible = SrcLim::is_signed || (DstLim::lowest() > SrcLim::lowest());

    // unsigned <-- unsigned
    if constexpr ((!DstLim::is_signed) && (!SrcLim::is_signed)) {
        if constexpr (positive_overflow_possible) {
            if (value > DstLim::max()) {
                return true;
            }
        }
    }
    // unsigned <-- signed
    else if constexpr ((!DstLim::is_signed) && SrcLim::is_signed) {
        if constexpr (positive_overflow_possible) {
            if (value > DstLim::max()) {
                return true;
            }
        }

        if constexpr (negative_overflow_possible) {
            if (value < 0) {
                return true;
            }
        }
    }
    // signed <-- unsigned
    else if constexpr (DstLim::is_signed && (!SrcLim::is_signed)) {
        if constexpr (positive_overflow_possible) {
            if (value > DstLim::max()) {
                return true;
            }
        }
    }
    // signed <-- signed
    else if constexpr (DstLim::is_signed && SrcLim::is_signed) {
        if constexpr (positive_overflow_possible) {
            if (value > DstLim::max()) {
                return true;
            }
        }

        if constexpr (negative_overflow_possible) {
            if (value < DstLim::lowest()) {
                return true;
            }
        }
    }

    return false;
}

template<typename Dst, typename Src>
constexpr Dst numeric_cast(const Src &value, const char *overflow_message = "value")
{
    if (numeric_cast_will_overflow<Dst, Src>(value)) {
        throw std::overflow_error(overflow_message);
    }

    return static_cast<Dst>(value);
}

// Memory streams, because C++ doesn't supply these.
struct membuf : std::streambuf
{
public:
    // construct membuf for reading and/or writing
    inline membuf(void *base, size_t size, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out)
    {
        auto cbase = reinterpret_cast<char *>(base);

        if (which & std::ios_base::in) {
            this->setg(cbase, cbase, cbase + size);
        }

        if (which & std::ios_base::out) {
            this->setp(cbase, cbase + size);
        }
    }

    // construct membuf for reading
    inline membuf(const void *base, size_t size, std::ios_base::openmode which = std::ios_base::in)
    {
        auto cbase = const_cast<char *>(reinterpret_cast<const char *>(base));

        if (which & std::ios_base::in) {
            this->setg(cbase, cbase, cbase + size);
        }
    }

protected:
    inline void setpptrs(char *first, char *next, char *end)
    {
        setp(first, end);
        pbump(next - first);
    }

    // seek operations
    pos_type seekpos(pos_type off, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override
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

    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
        std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override
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

    // put stuff
    std::streamsize xsputn(const char_type *s, std::streamsize n) override
    {
        if (pptr() == epptr()) {
            return traits_type::eof();
        }

        ptrdiff_t free_space = epptr() - pptr();
        std::streamsize num_write = std::min(free_space, n);

        memcpy(pptr(), s, n);
        setpptrs(pbase(), pptr() + n, epptr());

        return num_write;
    };

    int_type overflow(int_type ch) override { return traits_type::eof(); }

    // get stuff
    std::streamsize xsgetn(char_type *s, std::streamsize n) override
    {
        if (gptr() == egptr()) {
            return traits_type::eof();
        }

        ptrdiff_t free_space = egptr() - gptr();
        std::streamsize num_read = std::min(free_space, n);

        memcpy(s, gptr(), n);
        setg(eback(), gptr() + n, egptr());

        return num_read;
    };

    int_type underflow() override { return traits_type::eof(); }
};

struct memstream : virtual membuf, std::ostream, std::istream
{
    inline memstream(void *base, size_t size, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out)
        : membuf(base, size, which), std::ostream(static_cast<std::streambuf *>(this)),
          std::istream(static_cast<std::streambuf *>(this))
    {
    }

    inline memstream(const void *base, size_t size, std::ios_base::openmode which = std::ios_base::in)
        : membuf(base, size, which), std::ostream(nullptr), std::istream(static_cast<std::streambuf *>(this))
    {
    }
};

template<class T, class = void>
struct is_iterator : std::false_type
{
};

template<class T>
struct is_iterator<T,
    std::void_t<typename std::iterator_traits<T>::difference_type, typename std::iterator_traits<T>::pointer,
        typename std::iterator_traits<T>::reference, typename std::iterator_traits<T>::value_type,
        typename std::iterator_traits<T>::iterator_category>> : std::true_type
{
};

template<class T>
constexpr bool is_iterator_v = is_iterator<T>::value;

void CRC_Init(unsigned short *crcvalue);
void CRC_ProcessByte(unsigned short *crcvalue, uint8_t data);
unsigned short CRC_Value(unsigned short crcvalue);
unsigned short CRC_Block(const unsigned char *start, int count);

#include <bitset>

template<typename Enum>
struct bitflags
{
    static_assert(std::is_enum_v<Enum>, "Must be enum");

private:
    using type = typename std::underlying_type_t<Enum>;
    std::bitset<sizeof(type) * 8> _bits{};

    constexpr bitflags(const std::bitset<sizeof(type) * 8> &bits) : _bits(bits) { }

public:
    constexpr bitflags() { }

    constexpr bitflags(const Enum &enumValue) : _bits(static_cast<type>(enumValue)) { }

    constexpr bitflags(const bitflags &copy) = default;
    constexpr bitflags(bitflags &&move) = default;

    constexpr bitflags &operator=(const bitflags &copy) = default;
    constexpr bitflags &operator=(bitflags &&move) = default;

    inline explicit operator bool() const { return _bits.any(); }

    inline bool operator!() const { return !_bits.any(); }

    inline operator Enum() const { return static_cast<Enum>(_bits.to_ulong()); }

    inline bitflags &operator|=(const bitflags &r)
    {
        _bits |= r._bits;
        return *this;
    }
    inline bitflags &operator&=(const bitflags &r)
    {
        _bits &= r._bits;
        return *this;
    }
    inline bitflags &operator^=(const bitflags &r)
    {
        _bits ^= r._bits;
        return *this;
    }

    inline bitflags operator|(const bitflags &r) { return bitflags(*this) |= r; }
    inline bitflags operator&(const bitflags &r) { return bitflags(*this) &= r; }
    inline bitflags operator^(const bitflags &r) { return bitflags(*this) ^= r; }

    inline bitflags operator~() const { return ~_bits; }

    inline bool operator==(const bitflags &r) const { return _bits == r._bits; }
    inline bool operator!=(const bitflags &r) const { return _bits != r._bits; }

    inline bool operator==(const Enum &r) const { return _bits == bitflags(r)._bits; }
    inline bool operator!=(const Enum &r) const { return _bits != bitflags(r)._bits; }
};
