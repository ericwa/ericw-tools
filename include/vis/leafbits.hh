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
#include <common/bitflags.hh>

class leafbits_t
{
    size_t _size = 0;
    std::unique_ptr<uint32_t[]> bits{};

    constexpr size_t block_size() const { return (_size + mask) >> shift; }
    inline std::unique_ptr<uint32_t[]> allocate() { return std::make_unique<uint32_t[]>(block_size()); }
    constexpr size_t byte_size() const { return block_size() * sizeof(*bits.get()); }

public:
    static constexpr size_t shift = 5;
    static constexpr size_t mask = (sizeof(uint32_t) << 3) - 1UL;

    leafbits_t() = default;

    inline leafbits_t(size_t size)
        : _size(size),
          bits(allocate())
    {
    }

    inline leafbits_t(const leafbits_t &copy)
        : leafbits_t(copy._size)
    {
        memcpy(bits.get(), copy.bits.get(), byte_size());
    }

    inline leafbits_t(leafbits_t &&move) noexcept
        : _size(move._size),
          bits(std::move(move.bits))
    {
        move._size = 0;
    }

    inline leafbits_t &operator=(leafbits_t &&move) noexcept
    {
        _size = move._size;
        bits = std::move(move.bits);

        move._size = 0;

        return *this;
    }

    inline leafbits_t &operator=(const leafbits_t &copy)
    {
        resize(copy._size);
        memcpy(bits.get(), copy.bits.get(), byte_size());
        return *this;
    }

    constexpr size_t size() const { return _size; }

    // this clears existing bit data!
    inline void resize(size_t new_size) { *this = leafbits_t(new_size); }

    inline void clear() { memset(bits.get(), 0, byte_size()); }
    inline void setall() { memset(bits.get(), 0xff, byte_size()); }

    inline uint32_t *data() { return bits.get(); }
    inline const uint32_t *data() const { return bits.get(); }

    inline bool operator[](size_t index) const { return !!(bits[index >> shift] & nth_bit(index & mask)); }

    struct reference
    {
        std::unique_ptr<uint32_t[]> &bits;
        size_t block_index;
        size_t mask;

        inline explicit operator bool() const { return !!(bits[block_index] & mask); }

        inline reference &operator=(bool value)
        {
            if (value)
                bits[block_index] |= mask;
            else
                bits[block_index] &= ~mask;

            return *this;
        }
    };

    inline reference operator[](size_t index) { return {bits, index >> shift, nth_bit(index & mask)}; }
};
