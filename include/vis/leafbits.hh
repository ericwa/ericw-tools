/*  Copyright (C) 2012-2013 Kevin Shanahan

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

#include <cstdlib>
#include <cstring>
#include <common/cmdlib.hh>

/* Use some GCC builtins */
#if !defined(ffsl) && defined(__GNUC__)
#define ffsl __builtin_ffsl
#elif defined(WIN32)
#include <intrin.h>
inline int ffsl(long int val)
{
    unsigned long indexout;
    unsigned char res = _BitScanForward(&indexout, val);
    if (!res)
        return 0;
    else
        return indexout + 1;
}
#endif

class leafbits_t
{
    size_t _size;
    uint32_t *bits;

    constexpr size_t block_size() const { return (_size + mask) >> shift; }
    uint32_t *allocate() { return new uint32_t[block_size()]{}; }
    constexpr size_t byte_size() const { return block_size() * sizeof(*bits); }

public:
    static constexpr size_t shift = 5;
    static constexpr size_t mask = (sizeof(uint32_t) << 3) - 1UL;

    constexpr leafbits_t() : _size(0), bits(nullptr) { }

    leafbits_t(size_t size) : _size(size), bits(allocate()) { }

    leafbits_t(const leafbits_t &copy) : leafbits_t(copy._size) { memcpy(bits, copy.bits, byte_size()); }

    constexpr leafbits_t(leafbits_t &&move) noexcept : _size(move._size), bits(move.bits)
    {
        move._size = 0;
        move.bits = nullptr;
    }

    leafbits_t &operator=(leafbits_t &&move) noexcept
    {
        _size = move._size;
        bits = move.bits;

        move._size = 0;
        move.bits = nullptr;

        return *this;
    }

    leafbits_t &operator=(const leafbits_t &copy)
    {
        resize(copy._size);
        memcpy(bits, copy.bits, byte_size());
        return *this;
    }

    ~leafbits_t()
    {
        delete[] bits;
        bits = nullptr;
    }

    constexpr const size_t &size() const { return _size; }

    // this clears existing bit data!
    void resize(size_t new_size) { *this = leafbits_t(new_size); }

    void clear() { memset(bits, 0, byte_size()); }

    constexpr uint32_t *data() { return bits; }
    constexpr const uint32_t *data() const { return bits; }

    constexpr bool operator[](const size_t &index) const { return !!(bits[index >> shift] & (1UL << (index & mask))); }

    struct reference
    {
        uint32_t *bits;
        size_t block_index;
        size_t mask;

        constexpr operator bool() const { return !!(bits[block_index] & mask); }

        reference &operator=(bool value)
        {
            if (value)
                bits[block_index] |= mask;
            else
                bits[block_index] &= ~mask;

            return *this;
        }
    };

    constexpr reference operator[](const size_t &index) { return {bits, index >> shift, 1ULL << (index & mask)}; }
};
