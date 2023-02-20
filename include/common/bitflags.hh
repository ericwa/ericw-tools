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

#include <bitset>
#include <type_traits>

template<typename Enum>
struct bitflags
{
    static_assert(std::is_enum_v<Enum>, "Must be enum");

private:
    using type = typename std::underlying_type_t<Enum>;
    std::bitset<sizeof(type) * 8> _bits{};

    constexpr bitflags(const std::bitset<sizeof(type) * 8> &bits)
        : _bits(bits)
    {
    }

public:
    constexpr bitflags() { }

    constexpr bitflags(const Enum &enumValue)
        : _bits(static_cast<type>(enumValue))
    {
    }

    constexpr bitflags(const bitflags &copy) = default;
    constexpr bitflags(bitflags &&move) noexcept = default;

    constexpr bitflags &operator=(const bitflags &copy) = default;
    constexpr bitflags &operator=(bitflags &&move) noexcept = default;

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

// fetch integral representation of the value at bit N
template<typename T>
constexpr auto nth_bit(T l)
{
    return static_cast<T>(1) << l;
}