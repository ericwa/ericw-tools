/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

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

#ifndef QBSP_MATHLIB_HH
#define QBSP_MATHLIB_HH

#ifdef DOUBLEVEC_T
#define vec_t double
#define VECT_MAX DBL_MAX
#else
#define vec_t float
#define VECT_MAX FLT_MAX
#endif
typedef vec_t vec3_t[3];

extern const vec3_t vec3_origin;

bool VectorCompare(const vec3_t v1, const vec3_t v2, vec_t epsilon);

vec_t Q_rint(vec_t in);
extern vec_t DotProduct(const vec3_t v1, const vec3_t v2);
extern void VectorSubtract(const vec3_t va, const vec3_t vb, vec3_t out);
extern void VectorAdd(const vec3_t va, const vec3_t vb, vec3_t out);
extern void VectorCopy(const vec3_t in, vec3_t out);

vec_t VectorLengthSq(const vec3_t v);
vec_t VectorLength(const vec3_t v);

void VectorMA(const vec3_t va, const double scale, const vec3_t vb, vec3_t vc);

void CrossProduct(const vec3_t v1, const vec3_t v2, vec3_t cross);
vec_t VectorNormalize(vec3_t v);
void VectorInverse(vec3_t v);
void VectorScale(const vec3_t v, const vec_t scale, vec3_t out);

float SignedDegreesBetweenUnitVectors(const vec3_t start, const vec3_t end, const vec3_t normal);

#ifdef __GNUC__
/* min and max macros with type checking */
#define qmax(a,b) ({      \
    typeof(a) a_ = (a);   \
    typeof(b) b_ = (b);   \
    (void)(&a_ == &b_);   \
    (a_ > b_) ? a_ : b_;  \
})
#define qmin(a,b) ({      \
    typeof(a) a_ = (a);   \
    typeof(b) b_ = (b);   \
    (void)(&a_ == &b_);   \
    (a_ < b_) ? a_ : b_;  \
})
#else
#define qmax(a,b) (((a)>(b)) ? (a) : (b))
#define qmin(a,b) (((a)>(b)) ? (b) : (a))
#endif

#define stringify__(x) #x
#define stringify(x) stringify__(x)

#endif
