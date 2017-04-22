/*  Copyright (C) 2017 Eric Wasylishen

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

#ifndef __COMMON_QVEC_HH__
#define __COMMON_QVEC_HH__

#define qmax(a,b) (((a)>(b)) ? (a) : (b))
#define qmin(a,b) (((a)>(b)) ? (b) : (a))

template <int N, class T>
class qvec {
protected:
    T v[N];
    
public:
    qvec() {
        for (int i=0; i<N; i++)
            v[i] = 0;
    }

    qvec(const T &a) {
        for (int i=0; i<N; i++)
            v[i] = a;
    }
    
    qvec(const T &a, const T &b) {
        v[0] = a;
        if (1 < N)
            v[1] = b;
        for (int i=2; i<N; i++)
            v[i] = 0;
    }
    
    qvec(const T &a, const T &b, const T &c) {
        v[0] = a;
        if (1 < N)
            v[1] = b;
        if (2 < N)
            v[2] = c;
        for (int i=3; i<N; i++)
            v[i] = 0;
    }
    
    qvec(const T &a, const T &b, const T &c, const T &d) {
        v[0] = a;
        if (1 < N)
            v[1] = b;
        if (2 < N)
            v[2] = c;
        if (3 < N)
            v[3] = d;
        for (int i=4; i<N; i++)
            v[i] = 0;
    }
    
    template <int N2>
    qvec(const qvec<N2, T> &other) {
        const int minSize = qmin(N,N2);
        
        // truncates if `other` is longer than `this`
        for (int i=0; i<minSize; i++)
            v[i] = other[i];
        
        // zero-fill if `other` is smaller than `this`
        for (int i=minSize; i<N; i++)
            v[i] = 0;
    }

    bool operator==(const qvec<N,T> &other) const {
        for (int i=0; i<N; i++)
            if (v[i] != other.v[i])
                return false;
        return true;
    }
    
    T operator[](const size_t idx) const {
        return v[idx];
    }
    
    T &operator[](const size_t idx) {
        return v[idx];
    }
    
    void operator+=(const qvec<N,T> &other) {
        for (int i=0; i<N; i++)
            v[i] += other.v[i];
    }
    void operator-=(const qvec<N,T> &other) {
        for (int i=0; i<N; i++)
            v[i] -= other.v[i];
    }
    void operator*=(const T &scale) {
        for (int i=0; i<N; i++)
            v[i] *= scale;
    }
    void operator/=(const T &scale) {
        for (int i=0; i<N; i++)
            v[i] /= scale;
    }
    
    qvec<N,T> operator+(const qvec<N,T> &other) const {
        qvec<N,T> res(*this);
        res += other;
        return res;
    }
    
    qvec<N,T> operator-(const qvec<N,T> &other) const {
        qvec<N,T> res(*this);
        res -= other;
        return res;
    }
    
    qvec<N,T> operator*(const T &scale) const {
        qvec<N,T> res(*this);
        res *= scale;
        return res;
    }
    
    qvec<N,T> operator/(const T &scale) const {
        qvec<N,T> res(*this);
        res /= scale;
        return res;
    }
};

template <class T>
qvec<3,T> cross(const qvec<3,T> &v1, const qvec<3,T> &v2) {
    
    return qvec<3,T>(v1[1] * v2[2] - v1[2] * v2[1],
                     v1[2] * v2[0] - v1[0] * v2[2],
                     v1[0] * v2[1] - v1[1] * v2[0]);
}

template <int N, class T>
qvec<N,T> dot(const qvec<N,T> &v1, const qvec<N,T> &v2) {
    T result = 0;
    for (int i=0; i<N; i++) {
        result += v1[i] * v2[i];
    }
    return result;
}

using qvec2f = qvec<2, float>;
using qvec3f = qvec<3, float>;
using qvec4f = qvec<4, float>;

using qvec2d = qvec<2, double>;
using qvec3d = qvec<3, double>;
using qvec4d = qvec<4, double>;

#endif /* __COMMON_QVEC_HH__ */
