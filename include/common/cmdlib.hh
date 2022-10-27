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

#include <array>
#include <cstring> // for memcpy()
#include <string>
#include <string_view>
#include <vector>
#include <streambuf>
#include <istream>
#include <ostream>
#include <tuple> // for std::apply()

int32_t Q_strncasecmp(const std::string_view &a, const std::string_view &b, size_t maxcount);
int32_t Q_strcasecmp(const std::string_view &a, const std::string_view &b);
bool string_iequals(const std::string_view &a, const std::string_view &b); // mxd

struct case_insensitive_hash
{
    std::size_t operator()(const std::string &s) const noexcept;
};

struct case_insensitive_equal
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

struct case_insensitive_less
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

/**
 * standard C natural string compare
 * @param s1 left string
 * @param s2 right string
 * @return -1 when s1 < s2, 0 when s1 == s2, 1 when s1 > s2
 */
int natstrcmp(const char *s1, const char *s2, bool case_sensitive = true);

/**
 * STL natural less-than string compare
 * @param s1 left string
 * @param s2 right string
 * @return true when natural s1 < s2
 */
bool natstrlt(const char *s1, const char *s2, bool case_sensitive = true);

/**
 * @param s1 left string
 * @param s2 right string
 * std::string variant of natstrlt.
 * @return true when natural s1 < s2
 */
bool stlnatstrlt(const std::string &s1, const std::string &s2, bool case_sensitive = true);

struct natural_equal
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

struct natural_less
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

struct natural_case_insensitive_equal
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

struct natural_case_insensitive_less
{
    bool operator()(const std::string &l, const std::string &r) const noexcept;
};

std::string_view::const_iterator string_ifind(std::string_view haystack, std::string_view needle);
bool string_icontains(std::string_view haystack, std::string_view needle);

#include <chrono>

using qclock = std::chrono::high_resolution_clock;
using duration = std::chrono::duration<double>;
using time_point = std::chrono::time_point<qclock, duration>;

time_point I_FloatTime();

/*
 * ============================================================================
 *                            BYTE ORDER FUNCTIONS
 * ============================================================================
 */
// C++20 polyfill
#if defined(__cpp_lib_endian) && __cpp_lib_endian >= 201907L
#include <bit>
#else
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
#endif

// Binary streams; by default, streams use the native endianness
// (unchanged bytes) but can be changed to a specific endianness
// with the manipulator below.
namespace detail
{
int32_t endian_i();

// 0 is the default for iwords
enum class st_en : long
{
    na = 0,
    le = 1,
    be = 2,
};

bool need_swap(std::ios_base &os);

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

// blank type used for paddings
template<size_t n>
struct padding
{
};

struct padding_n
{
    size_t n;

    constexpr padding_n(size_t np) : n(np) { }
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

inline std::ostream &operator<=(std::ostream &s, const padding_n &p)
{
    for (size_t i = 0; i < p.n; i++) {
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

template<typename T>
inline std::enable_if_t<std::is_enum_v<T>, std::ostream &> operator<=(std::ostream &s, const T &obj)
{
    s <= reinterpret_cast<const std::underlying_type_t<T> &>(obj);
    return s;
}

template<size_t n>
inline std::istream &operator>=(std::istream &s, padding<n> &)
{
    s.seekg(n, std::ios_base::cur);

    return s;
}

template<size_t n>
inline std::istream &operator>=(std::istream &s, padding_n &p)
{
    s.seekg(p.n, std::ios_base::cur);

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

template<typename T>
inline std::enable_if_t<std::is_enum_v<T>, std::istream &> operator>=(std::istream &s, T &obj)
{
    s >= reinterpret_cast<std::underlying_type_t<T> &>(obj);
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
    membuf(void *base, size_t size, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out);

    // construct membuf for reading
    membuf(const void *base, size_t size, std::ios_base::openmode which = std::ios_base::in);

protected:
    void setpptrs(char *first, char *next, char *end);

    // seek operations
    pos_type seekpos(pos_type off, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override;
    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
        std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override;

    // put stuff
    std::streamsize xsputn(const char_type *s, std::streamsize n) override;
    int_type overflow(int_type ch) override;

    // get stuff
    std::streamsize xsgetn(char_type *s, std::streamsize n) override;

    int_type underflow() override;
};

struct memstream : virtual membuf, std::ostream, std::istream
{
    memstream(void *base, size_t size,
        std::ios_base::openmode which = std::ios_base::in | std::ios_base::out | std::ios_base::binary);

    memstream(
        const void *base, size_t size, std::ios_base::openmode which = std::ios_base::in | std::ios_base::binary);
};

struct omemstream : virtual membuf, std::ostream
{
    omemstream(void *base, size_t size,
        std::ios_base::openmode which = std::ios_base::in | std::ios_base::out | std::ios_base::binary);
};

struct imemstream : virtual membuf, std::istream
{
    imemstream(
        const void *base, size_t size, std::ios_base::openmode which = std::ios_base::in | std::ios_base::binary);
};

// A very strange stream buffer that just stores the written size.
// It can only write, not read.
struct omemsizebuf : std::streambuf
{
public:
    // construct membuf for writing
    omemsizebuf(std::ios_base::openmode which = std::ios_base::out);

protected:
    void setpptrs(char *first, char *next, char *end);

    // seek operations
    pos_type seekpos(pos_type off, std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override;

    pos_type seekoff(off_type off, std::ios_base::seekdir dir,
        std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) override;

    // put stuff
    std::streamsize xsputn(const char_type *s, std::streamsize n) override;

    int_type overflow(int_type ch) override;
};

struct omemsizestream : virtual omemsizebuf, std::ostream
{
    omemsizestream(
        std::ios_base::openmode which = std::ios_base::out | std::ios_base::binary);
};

void CRC_Init(uint16_t &crcvalue);
void CRC_ProcessByte(uint16_t &crcvalue, uint8_t data);
uint16_t CRC_Block(const uint8_t *start, int count);

void *q_aligned_malloc(size_t align, size_t size);
void q_aligned_free(void *ptr);

/**
 * Allocator for aligned data.
 *
 * Modified from the Mallocator from Stephan T. Lavavej.
 * <http://blogs.msdn.com/b/vcblog/archive/2008/08/28/the-mallocator.aspx>
 */
template<typename T, std::size_t Alignment>
class aligned_allocator
{
public:
    // The following will be the same for virtually all allocators.
    using pointer = T *;
    using const_pointer = const T *;
    using reference = T &;
    using const_reference = const T &;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = ptrdiff_t;

    T *address(T &r) const { return &r; }

    const T *address(const T &s) const { return &s; }

    std::size_t max_size() const
    {
        // The following has been carefully written to be independent of
        // the definition of size_t and to avoid signed/unsigned warnings.
        return (static_cast<std::size_t>(0) - static_cast<std::size_t>(1)) / sizeof(T);
    }

    // The following must be the same for all allocators.
    template<typename U>
    struct rebind
    {
        typedef aligned_allocator<U, Alignment> other;
    };

    bool operator!=(const aligned_allocator &other) const { return !(*this == other); }

    void construct(T *const p, const T &t) const
    {
        void *const pv = reinterpret_cast<void *>(p);
        new (pv) T(t);
    }

    void destroy(T *const p) const { p->~T(); }

    // Returns true if and only if storage allocated from *this
    // can be deallocated from other, and vice versa.
    // Always returns true for stateless allocators.
    bool operator==(const aligned_allocator &other) const { return true; }

    // Default constructor, copy constructor, rebinding constructor, and destructor.
    // Empty for stateless allocators.
    aligned_allocator() { }
    aligned_allocator(const aligned_allocator &) { }
    template<typename U>
    aligned_allocator(const aligned_allocator<U, Alignment> &)
    {
    }
    ~aligned_allocator() { }

    // The following will be different for each allocator.
    T *allocate(const std::size_t n) const
    {
        // The return value of allocate(0) is unspecified.
        // Mallocator returns NULL in order to avoid depending
        // on malloc(0)'s implementation-defined behavior
        // (the implementation can define malloc(0) to return NULL,
        // in which case the bad_alloc check below would fire).
        // All allocators can return NULL in this case.
        if (n == 0) {
            return nullptr;
        }

        // All allocators should contain an integer overflow check.
        // The Standardization Committee recommends that std::length_error
        // be thrown in the case of integer overflow.
        if (n > max_size()) {
            throw std::length_error("aligned_allocator<T>::allocate() - Integer overflow.");
        }

        // Mallocator wraps malloc().
        void *const pv = q_aligned_malloc(Alignment, n * sizeof(T));

        // Allocators should throw std::bad_alloc in the case of memory allocation failure.
        if (pv == nullptr) {
            throw std::bad_alloc();
        }

        return reinterpret_cast<T *>(pv);
    }

    void deallocate(T *const p, const std::size_t n) const { q_aligned_free(p); }

    // The following will be the same for all allocators that ignore hints.
    template<typename U>
    T *allocate(const std::size_t n, const U * /* const hint */) const
    {
        return allocate(n);
    }

    // Allocators are not required to be assignable, so
    // all allocators should have a private unimplemented
    // assignment operator. Note that this will trigger the
    // off-by-default (enabled under /Wall) warning C4626
    // "assignment operator could not be generated because a
    // base class assignment operator is inaccessible" within
    // the STL headers, but that warning is useless.
private:
    aligned_allocator &operator=(const aligned_allocator &);
};

template<typename T>
using aligned_vector = std::vector<T, aligned_allocator<T, alignof(T)>>;