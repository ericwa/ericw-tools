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

#include <limits>
#include <stdexcept>

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

// helper functions to quickly numerically cast mins/maxs
// and floor/ceil them in the case of float -> integral
template<typename T, typename F>
inline qvec<T, 3> aabb_mins_cast(const qvec<F, 3> &f, const char *overflow_message = "mins")
{
    if constexpr (std::is_floating_point_v<F> && !std::is_floating_point_v<T>)
        return {numeric_cast<T>(floor(f[0]), overflow_message), numeric_cast<T>(floor(f[1]), overflow_message),
            numeric_cast<T>(floor(f[2]), overflow_message)};
    else
        return {numeric_cast<T>(f[0], overflow_message), numeric_cast<T>(f[1], overflow_message),
            numeric_cast<T>(f[2], overflow_message)};
}

template<typename T, typename F>
inline qvec<T, 3> aabb_maxs_cast(const qvec<F, 3> &f, const char *overflow_message = "maxs")
{
    if constexpr (std::is_floating_point_v<F> && !std::is_floating_point_v<T>)
        return {numeric_cast<T>(ceil(f[0]), overflow_message), numeric_cast<T>(ceil(f[1]), overflow_message),
            numeric_cast<T>(ceil(f[2]), overflow_message)};
    else
        return {numeric_cast<T>(f[0], overflow_message), numeric_cast<T>(f[1], overflow_message),
            numeric_cast<T>(f[2], overflow_message)};
}

// shortcut template to trim (& convert) std::arrays
// between two lengths
template<typename ADest, typename ASrc>
constexpr ADest array_cast(const ASrc &src, const char *overflow_message = "src")
{
    ADest dest{};

    for (size_t i = 0; i < std::min(dest.size(), src.size()); i++) {
        if constexpr (std::is_arithmetic_v<typename ADest::value_type> &&
                      std::is_arithmetic_v<typename ASrc::value_type>)
            dest[i] = numeric_cast<typename ADest::value_type>(src[i], overflow_message);
        else
            dest[i] = static_cast<typename ADest::value_type>(src[i]);
    }

    return dest;
}

// converts from a flexible mface_t styles vector, to a game-specific fixed-size std::array.
// expects the vector to have be resized to match gamedef_t::num_styles() already; throws if misused.
template<class ArrClass>
ArrClass styles_vec_to_array(const std::vector<uint8_t> &vec)
{
    static_assert(ArrClass().size() != 0);

    if (vec.size() != ArrClass().size()) {
        throw std::runtime_error("style vector was not the correct size");
    }

    ArrClass result;
    for (size_t i = 0; i < vec.size(); ++i) {
        result[i] = vec[i];
    }
    return result;
}

// converts a game-specific fixed-size std::array of style values, to a vector for use in mface_t
template<class ArrClass>
std::vector<uint8_t> styles_array_to_vec(const ArrClass &array)
{
    return std::vector<uint8_t>(array.begin(), array.end());
}
