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

#include <initializer_list>
#include <cassert>
#include <cmath>
#include <string>

#ifndef qmax // FIXME: Remove this ifdef
#define qmax(a,b) (((a)>(b)) ? (a) : (b))
#define qmin(a,b) (((a)>(b)) ? (b) : (a))
#define qclamp(val, min, max) (qmax(qmin((val), (max)), (min)))
#endif

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
    
    /**
     * Casting from another vector type of the same length
     */
    template <class T2>
    qvec(const qvec<N, T2> &other) {
        for (int i=0; i<N; i++)
            v[i] = static_cast<T>(other[i]);
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

    /**
     * Extending a vector
     */
    qvec(const qvec<N-1, T> &other, T value) {
        for (int i = 0; i < N - 1; ++i) {
            v[i] = other[i];
        }
        v[N-1] = value;
    }

    bool operator==(const qvec<N,T> &other) const {
        for (int i=0; i<N; i++)
            if (v[i] != other.v[i])
                return false;
        return true;
    }
    
    bool operator!=(const qvec<N,T> &other) const {
        return !(*this == other);
    }
    
    T operator[](const int idx) const {
        assert(idx >= 0 && idx < N);
        return v[idx];
    }
    
    T &operator[](const int idx) {
        assert(idx >= 0 && idx < N);
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

    qvec<N,T> operator-() const {
        qvec<N,T> res(*this);
        res *= -1;
        return res;
    }

    qvec<3, T> xyz() const {
        static_assert(N >= 3);
        return qvec<3, T>(*this);
    }
};

namespace qv {
    template <class T>
    qvec<3,T> cross(const qvec<3,T> &v1, const qvec<3,T> &v2) {
        return qvec<3,T>(v1[1] * v2[2] - v1[2] * v2[1],
                         v1[2] * v2[0] - v1[0] * v2[2],
                         v1[0] * v2[1] - v1[1] * v2[0]);
    }

    template <int N, class T>
    T dot(const qvec<N,T> &v1, const qvec<N,T> &v2) {
        T result = 0;
        for (int i=0; i<N; i++) {
            result += v1[i] * v2[i];
        }
        return result;
    }

    template <int N, class T>
    qvec<N,T> floor(const qvec<N,T> &v1) {
        qvec<N,T> res;
        for (int i=0; i<N; i++) {
            res[i] = std::floor(v1[i]);
        }
        return res;
    }
    
    template <int N, class T>
    qvec<N,T> pow(const qvec<N,T> &v1, const qvec<N,T> &v2) {
        qvec<N,T> res;
        for (int i=0; i<N; i++) {
            res[i] = std::pow(v1[i], v2[i]);
        }
        return res;
    }
    
    template <int N, class T>
    qvec<N,T> min(const qvec<N,T> &v1, const qvec<N,T> &v2) {
        qvec<N,T> res;
        for (int i=0; i<N; i++) {
            res[i] = qmin(v1[i], v2[i]);
        }
        return res;
    }
    
    template <int N, class T>
    qvec<N,T> max(const qvec<N,T> &v1, const qvec<N,T> &v2) {
        qvec<N,T> res;
        for (int i=0; i<N; i++) {
            res[i] = qmax(v1[i], v2[i]);
        }
        return res;
    }
    
    template <int N, class T>
    T length2(const qvec<N,T> &v1) {
        T len2 = 0;
        for (int i=0; i<N; i++) {
            len2 += (v1[i] * v1[i]);
        }
        return len2;
    }
    
    template <int N, class T>
    T length(const qvec<N,T> &v1) {
        return std::sqrt(length2(v1));
    }
    
    template <int N, class T>
    qvec<N,T> normalize(const qvec<N,T> &v1) {
        return v1 / length(v1);
    }
    
    template <int N, class T>
    T distance(const qvec<N,T> &v1, const qvec<N,T> &v2) {
        return length(v2 - v1);
    }
    
    std::string to_string(const qvec<3,float> &v1);
    
    template <int N, class T>
    bool epsilonEqual(const qvec<N,T> &v1, const qvec<N,T> &v2, T epsilon) {
        for (int i=0; i<N; i++) {
            T diff = v1[i] - v2[i];
            if (fabs(diff) > epsilon)
                return false;
        }
        return true;
    }

    template <int N, class T>
    int indexOfLargestMagnitudeComponent(const qvec<N,T> &v)
    {
        int largestIndex = 0;
        T largestMag = 0;

        for (int i=0; i<N; ++i) {
            const T currentMag = std::fabs(v[i]);

            if (currentMag > largestMag) {
                largestMag = currentMag;
                largestIndex = i;
            }
        }

        return largestIndex;
    }
};


using qvec2f = qvec<2, float>;
using qvec3f = qvec<3, float>;
using qvec4f = qvec<4, float>;

using qvec2d = qvec<2, double>;
using qvec3d = qvec<3, double>;
using qvec4d = qvec<4, double>;

using qvec2i = qvec<2, int>;

template <class T>
class qplane3 {
private:
    qvec<3, T> m_normal;
    T m_dist;
    
public:
    qplane3(const qvec<3, T> &normal, const T &dist)
    : m_normal(normal),
      m_dist(dist) {}
        
    T distAbove(const qvec<3, T> &pt) const { return qv::dot(pt, m_normal) - m_dist; }
    const qvec<3, T> &normal() const { return m_normal; }
    const T dist() const { return m_dist; }
    
    const qvec<4, T> vec4() const { return qvec<4, T>(m_normal[0], m_normal[1], m_normal[2], m_dist); }
};

using qplane3f = qplane3<float>;
using qplane3d = qplane3<double>;

/**
 * M row, N column matrix.
 */
template <int M, int N, class T>
class qmat {
public:
    /** 
     * Column-major order. [ (row0,col0), (row1,col0), .. ]
     */
    T m_values[M*N];
    
public:
    /**
     * Identity matrix if square, otherwise fill with 0
     */
    qmat() {
        for (int i=0; i<M*N; i++)
            m_values[i] = 0;
        
        if (M == N) {
            // identity matrix
            for (int i=0; i<N; i++) {
                this->at(i,i) = 1;
            }
        }
    }
    
    /**
     * Fill with a value
     */
    qmat(T val) {
        for (int i=0; i<M*N; i++)
            m_values[i] = val;
    }
    
    // copy constructor
    qmat(const qmat<M,N,T> &other) {
        for (int i=0; i<M*N; i++)
            this->m_values[i] = other.m_values[i];
    }

    /**
     * Casting from another matrix type of the same size
     */
    template <class T2>
    qmat(const qmat<M, N, T2> &other) {
        for (int i=0; i<M*N; i++)
            this->m_values[i] = static_cast<T>(other.m_values[i]);
    }

    // initializer list, column-major order
    qmat(std::initializer_list<T> list) {
        assert(list.size() == M*N);
        const T *listPtr = list.begin();
        
        for (int i=0; i<M*N; i++) {
            this->m_values[i] = listPtr[i];
        }
    }

    bool operator==(const qmat<M,N,T> &other) const {
        for (int i=0; i<M*N; i++)
            if (this->m_values[i] != other.m_values[i])
                return false;
        return true;
    }
    
    // access to elements
    
    T& at(int row, int col) {
        assert(row >= 0 && row < M);
        assert(col >= 0 && col < N);
        return m_values[col * M + row];
    }
    
    T at(int row, int col) const {
        assert(row >= 0 && row < M);
        assert(col >= 0 && col < N);
        return m_values[col * M + row];
    }
    
    // hacky accessor for mat[col][row] access
    const T* operator[](int col) const {
        assert(col >= 0 && col < N);
        return &m_values[col * M];
    }
    
    T* operator[](int col) {
        assert(col >= 0 && col < N);
        return &m_values[col * M];
    }
    
    // multiplication by a vector
    
    qvec<M,T> operator*(const qvec<N, T> &vec) const {
        qvec<M,T> res(0);
        for (int i=0; i<M; i++) { // for each row
            for (int j=0; j<N; j++) { // for each col
                res[i] += this->at(i, j) * vec[j];
            }
        }
        return res;
    }
    
    // multiplication by a matrix
    
    template<int P>
    qmat<M,P,T> operator*(const qmat<N, P, T> &other) const {
        qmat<M,P,T> res;
        for (int i=0; i<M; i++) {
            for (int j=0; j<P; j++) {
                T val = 0;
                for (int k=0; k<N; k++) {
                    val += this->at(i,k) * other.at(k,j);
                }
                res.at(i,j) = val;
            }
        }
        return res;
    }
    
    // multiplication by a scalar
    
    qmat<M,N,T> operator*(const T scalar) const {
        qmat<M,N,T> res(*this);
        for (int i=0; i<M*N; i++) {
            res.m_values[i] *= scalar;
        }
        return res;
    }
};

using qmat2x2f = qmat<2, 2, float>;
using qmat2x3f = qmat<2, 3, float>;
using qmat2x4f = qmat<2, 4, float>;

using qmat3x2f = qmat<3, 2, float>;
using qmat3x3f = qmat<3, 3, float>;
using qmat3x4f = qmat<3, 4, float>;

using qmat4x2f = qmat<4, 2, float>;
using qmat4x3f = qmat<4, 3, float>;
using qmat4x4f = qmat<4, 4, float>;


using qmat2x2d = qmat<2, 2, double>;
using qmat2x3d = qmat<2, 3, double>;
using qmat2x4d = qmat<2, 4, double>;

using qmat3x2d = qmat<3, 2, double>;
using qmat3x3d = qmat<3, 3, double>;
using qmat3x4d = qmat<3, 4, double>;

using qmat4x2d = qmat<4, 2, double>;
using qmat4x3d = qmat<4, 3, double>;
using qmat4x4d = qmat<4, 4, double>;

namespace qv {
    /**
     * These return a matrix filled with NaN if there is no inverse.
     */
    qmat4x4f inverse(const qmat4x4f &input);
    qmat4x4d inverse(const qmat4x4d &input);
    
    qmat2x2f inverse(const qmat2x2f &input);
};

#endif /* __COMMON_QVEC_HH__ */
