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

#include <vector>

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
